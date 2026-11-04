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

#define _GNU_SOURCE
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <dlfcn.h>
#include <time.h>
#include <memory>
#include <gflags/gflags.h>
#include "brpc/ubshm/timer/timer_mgr.h"
#include "brpc/ubshm/common/thread_lock.h"
#include "brpc/ubshm/common/common.h"
#include "brpc/ubshm/shm/shm_def.h"
#include "brpc/ubshm/ub_ring_manager.h"
#include "brpc/ubshm/ubs_mem/ubs_mem.h"
#include "brpc/ubshm/ubs_mem/ubs_mem_def.h"
#ifdef UT
#include "ubs_mem.h"
#endif
#include "shm_ubs.h"
#include "brpc/reloadable_flags.h"

namespace brpc {
namespace ubring {
#define UBRING_MK_UBSM(ret, fn, args) ret (*fn) args = nullptr
#include "brpc/ubshm/ubs_mem/declare_shm_ubs.h"
#define SHM_RIGHT_MODE 0666
#define UBRING_REGION_NAME_PREFIX "UbrONE2ALLRegion"
DEFINE_uint32(node_location, 1, "Location of the ub machine.");
DEFINE_bool(shm_wr_delay_comp, true, "Indicates whether to enable the write relay."
            "0: relay; 1: non-relay.");
DEFINE_int32(ub_flying_io_timeout_s, 5,
             "Time in seconds to wait for stopping data sending and receiving "
             "when the link is disconnected.");
BRPC_VALIDATE_GFLAG(ub_flying_io_timeout_s, brpc::PositiveInteger);
char g_region_name[MAX_REGION_NAME_DESC_LENGTH] = {0};
int g_shm_timer_fd = 0;
ShmList *g_shm_list = nullptr;
static RETURN_CODE UbsShmInterfacesLoad(void);
char hostname[MAX_HOST_NAME_DESC_LENGTH];

RETURN_CODE UbsShmInterfacesLoad(void)
{
#ifndef UT
    const char *ubsm_sdk_location = "/usr/local/ubs_mem/lib/libubsm_sdk.so";
#if defined(OS_LINUX)
    void* dlhandler = dlmopen(LM_ID_NEWLM, ubsm_sdk_location, RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE | RTLD_DEEPBIND);
#elif defined(OS_MACOSX)
    void* dlhandler = dlopen(ubsm_sdk_location, RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
#endif
    if (dlhandler == nullptr) {
        LOG(ERROR) << "Dlopen libubsm_sdk.so in " << ubsm_sdk_location << " failed, error:" << dlerror();
        return UBRING_ERR;
    }

#define UBRING_MK_UBSM_OPTIONAL(ret, fn, args)                                          \
    do {                                                                             \
        fn = (decltype(fn))dlsym(dlhandler, #fn);                                                  \
    } while (0)

#define UBRING_MK_UBSM(ret, fn, args)                                                   \
    do {                                                                             \
        if ((fn) != nullptr) {                                                          \
            break;                                                                   \
        }                                                                            \
        UBRING_MK_UBSM_OPTIONAL(ret, fn, args);                                         \
        if ((fn) == nullptr) {                                                          \
            LOG(ERROR) << "Fail load ubs_mem func " << #fn <<" error:" << dlerror(); \
            return UBRING_ERR;                                                          \
        }                                                                            \
    } while (0)
#include "brpc/ubshm/ubs_mem/declare_shm_ubs.h"

    dlclose(dlhandler);
    dlhandler = nullptr;
#endif
    return UBRING_OK;
}

static RETURN_CODE CreateUbsShmRegion(const char *region_name)
{
    int ret = snprintf(g_region_name, MAX_REGION_NAME_DESC_LENGTH, "%s_%u",
        UBRING_REGION_NAME_PREFIX, FLAGS_node_location);
    if (ret < 0) {
        LOG(ERROR) << "Snprintf_s region name failed, ret=" << ret;
        return UBRING_ERR;
    }

    ubsmem_regions_t regions = {0}; // 16 * (48 + 1) bytes, about 0.8k
    ret = ubsmem_lookup_regions(&regions);
    if (ret != UBSM_OK || regions.region[0].host_num <= 0) {
        LOG(ERROR) << "Ubs lookup share region failed, ret=" << ret << ", region.num=" << regions.region[0].host_num;
        return UBRING_ERR;
    }
    ubsmem_region_attributes_t region_attr = {0};
    region_attr.host_num = regions.region[0].host_num;
    for (int i = 0; i < region_attr.host_num; i++) {
        strcpy(region_attr.hosts[i].host_name, regions.region[0].hosts[i].host_name);
        region_attr.hosts[i].affinity = (strcmp(region_attr.hosts[i].host_name, hostname) == 0) ?
            true : false;
    }

    ret = ubsmem_create_region(region_name, 0, &region_attr);
    if (ret == UBSM_ERR_ALREADY_EXIST) {
        LOG(WARNING) << "Ubs region exists, region_name=" << region_name;
        return UBRING_OK;
    } else if (ret != UBSM_OK) {
        LOG(ERROR) << "Ubsmem create region failed, ret=" << ret;
        return UBRING_ERR;
    }

    return UBRING_OK;
}

static uint64_t AquireFlagIfWrDelayComp(const uint64_t flag)
{
    if (FLAGS_shm_wr_delay_comp == 0) {
        return flag;
    }
    return flag | UBSM_FLAG_WR_DELAY_COMP;
}

// Each task owns a copy of the mapping and its remaining operations.
static bool TryUbsCleanup(ShmListNode* task) {
    if (task->unmap) {
        const int ret = ubsmem_shmem_unmap(task->shm.addr, task->shm.len);
        if (ret != UBSM_OK) {
            LOG_EVERY_SECOND(WARNING) << "Ubs unmap shm=" << task->shm.name
                                     << " failed, ret=" << ret;
            return false;
        }
        task->unmap = false;
    }
    if (task->deallocate) {
        const int ret = ubsmem_shmem_deallocate(task->shm.name);
        if (ret != UBSM_OK && ret != UBSM_ERR_NOT_FOUND) {
            LOG_EVERY_SECOND(WARNING) << "Ubs deallocate shm=" << task->shm.name
                                     << " failed, ret=" << ret;
            return false;
        }
        task->deallocate = false;
    }
    return true;
}

static RETURN_CODE ReleaseUbsShmLocked(ShmList* list, SHM* shm,
                                      ShmListNode* task) {
    if (list != nullptr) {
        for (ShmListNode* node = list->head; node != nullptr; node = node->next) {
            if (node->shm.addr == task->shm.addr &&
                node->shm.len == task->shm.len &&
                strcmp(node->shm.name, task->shm.name) == 0) {
                node->deallocate |= task->deallocate;
                free(task);
                shm->addr = nullptr;
                return UBRING_OK;
            }
        }
    }
    if (TryUbsCleanup(task)) {
        free(task);
    } else if (list != nullptr) {
        task->next = nullptr;
        task->prev = list->tail;
        if (list->tail != nullptr) {
            list->tail->next = task;
        } else {
            list->head = task;
        }
        list->tail = task;
        ++list->size;
    } else {
        if (!task->unmap) {
            shm->addr = nullptr;
        }
        free(task);
        return UBRING_ERR;
    }
    shm->addr = nullptr;
    return UBRING_OK;
}

// Success means cleanup completed or its remaining operations were queued.
static RETURN_CODE ReleaseUbsShm(SHM* shm, bool deallocate) {
    // Reserve retry storage before changing the SDK mapping.
    auto* task = static_cast<ShmListNode*>(malloc(sizeof(ShmListNode)));
    if (task == nullptr) {
        return UBRING_ERR;
    }
    task->shm = *shm;
    task->unmap = shm->addr != nullptr;
    task->deallocate = deallocate;
    ShmList* list = g_shm_list;
    if (list != nullptr) {
        LOCK_GUARD(list->shm_lock);
        return ReleaseUbsShmLocked(list, shm, task);
    }
    return ReleaseUbsShmLocked(nullptr, shm, task);
}

RETURN_CODE UbsShmLocalMalloc(SHM* shm) {
    ShmList* list = g_shm_list;
    if (list == nullptr) {
        return UBRING_ERR;
    }
    LOCK_GUARD(list->shm_lock);
    for (ShmListNode* task = list->head; task != nullptr; task = task->next) {
        if (strcmp(task->shm.name, shm->name) == 0) {
            return SHM_ERR_EXIST;
        }
    }
    // Reserve rollback storage before creating a named SDK resource.
    std::unique_ptr<ShmListNode, decltype(&free)> rollback(
        static_cast<ShmListNode*>(malloc(sizeof(ShmListNode))), &free);
    if (!rollback) {
        return UBRING_ERR;
    }
    const uint64_t flags = AquireFlagIfWrDelayComp(
        UBSM_FLAG_ONLY_IMPORT_NONCACHE | UBSM_FLAG_MEM_ANONYMOUS);
    int ret = ubsmem_shmem_allocate(
        g_region_name, shm->name, shm->len, SHM_RIGHT_MODE, flags);
    if (ret == UBSM_ERR_ALREADY_EXIST) {
        return SHM_ERR_EXIST;
    }
    if (ret != UBSM_OK) {
        LOG(ERROR) << "Ubs allocate shm=" << shm->name << " failed, ret=" << ret;
        return SHM_ERR;
    }
    shm->addr = nullptr;
    ret = ubsmem_shmem_map(nullptr, shm->len, PROT_READ | PROT_WRITE,
                          MAP_SHARED, shm->name, 0,
                          reinterpret_cast<void**>(&shm->addr));
    if (ret != UBSM_OK) {
        LOG(ERROR) << "Ubs map shm=" << shm->name << " failed, ret=" << ret;
        rollback->shm = *shm;
        rollback->unmap = false;
        rollback->deallocate = true;
        ReleaseUbsShmLocked(list, shm, rollback.release());
        return ret == UBSM_ERR_NOT_FOUND ? SHM_ERR_NOT_FOUND : SHM_ERR;
    }
    shm->memid = 1;
    return UBRING_OK;
}

RETURN_CODE UbsShmMunmap(SHM *shm) {
    return ReleaseUbsShm(shm, false);
}

RETURN_CODE UbsShmFree(SHM *shm)
{
    if (shm->addr == nullptr) {
        LOG(ERROR) << "Ubs input shm param is invalid, addr is NULL.";
        return SHM_ERR_INPUT_INVALID;
    }

    // free
    int ret = ubsmem_shmem_deallocate(shm->name);
    if (ret != UBSM_OK) {
        if (ret == UBSM_ERR_IN_USING) {
            LOG(INFO) << "Ubs free shm=" << shm->name << " failed, resource attached=" << ret;
            return SHM_ERR_RESOURCE_ATTACHED;
        } else if (ret == UBSM_ERR_NOT_FOUND) {
            LOG(INFO) << "Ubs free shm=" << shm->name << " failed, resource not found=" << ret;
            return SHM_ERR_NOT_FOUND;
        }
        LOG(ERROR) << "Ubs free shm="<< shm->name << " failed, ret=" << ret;
        return SHM_ERR;
    }
    shm->addr = nullptr;
    LOG(INFO) << "Ubs free shm=" << shm->name << " length=" << shm->len << " success.";
    return UBRING_OK;
}

RETURN_CODE UbsShmLocalFree(SHM *shm) {
    return ReleaseUbsShm(shm, true);
}

RETURN_CODE UbsShmRemoteMalloc(SHM *shm)
{
    int ret = ubsmem_shmem_map(nullptr, shm->len, PROT_READ | PROT_WRITE, MAP_SHARED, shm->name, 0, (void**)&(shm->addr));
    if (ret != UBSM_OK) {
        LOG(ERROR) << "Ubs map Shm=" << shm->name << " failed, ret=" << ret;
        return SHM_ERR;
    }

    LOG(INFO) << "Ubs malloc remote shm=" << shm->name << " length=" << shm->len << " success.";
    return UBRING_OK;
}

RETURN_CODE UbsShmLocalMmap(SHM *shm, int prot)
{
    int ret = ubsmem_shmem_map(nullptr, shm->len, prot, MAP_SHARED, shm->name, 0, (void**)&(shm->addr));
    if (ret != UBSM_OK) {
        LOG(ERROR) << "Ubs map Shm=" << shm->name << " failed, ret=" << ret;
        return SHM_ERR;
    }

    LOG(INFO) << "Ubs mmap remote shm=" << shm->name << " length=" << shm->len << " success.";
    return UBRING_OK;
}

RETURN_CODE UbsShmRemoteFree(SHM *shm) {
    return ReleaseUbsShm(shm, false);
}

void UbsMemLoggerPrint(int level, const char *msg)
{
    if (level == UBSM_LOG_ERROR_LEVEL) {
        LOG(ERROR) << msg;
    } else if (level == UBSM_LOG_WARN_LEVEL) {
        LOG(WARNING) << msg;
    } else {
        LOG(INFO) << msg;
    }
    return;
}

RETURN_CODE UbsShmInit(void)
{
    // load libubsm_sdk.so and get function pointer
    RETURN_CODE ret_code = UbsShmInterfacesLoad();
    if (ret_code != UBRING_OK) {
        LOG(ERROR) << "Load ubs shm functions failed, ret=" << ret_code;
        return UBRING_ERR;
    }

    if (gethostname(hostname, MAX_HOST_NAME_DESC_LENGTH) != 0) {
        LOG(ERROR) << "ubring config gethostname failed, errno=" << errno;
        return UBRING_ERR;
    }

    int ret = ubsmem_set_extern_logger(UbsMemLoggerPrint);
    if (ret != UBSM_OK) {
        LOG(ERROR) << "Ubs set logger failed, ret=" << ret;
        return UBRING_ERR;
    }

    ret = ubsmem_set_logger_level(UBSM_LOG_INFO_LEVEL);
    if (ret != UBSM_OK) {
        LOG(ERROR) << "Ubs set logger level failed, ret=" << ret;
        return UBRING_ERR;
    }

    ubsmem_options_t options = {};
    ret = ubsmem_init_attributes(&options);
    if (ret != UBSM_OK) {
        LOG(ERROR) << "Ubs shm init attributes failed, ret=" << ret;
        return UBRING_ERR;
    }

    ret = ubsmem_initialize(&options);
    if (ret != UBSM_OK) {
        LOG(ERROR) << "Ubs shm initialize failed, ret=" << ret;
        return UBRING_ERR;
    }

    if (UNLIKELY(ubsmem_local_nid_query(&FLAGS_node_location) != UBSM_OK)) {
        LOG(ERROR) << "Get local nid failed.";
        return UBRING_ERR;
    }

    if (UNLIKELY(ubsmem_shmem_faults_register(brpc::ubring::UBRingManager::UbEventCallback) != UBSM_OK)) {
        LOG(ERROR) << "Failed to register the ub event callback function.";
        return UBRING_ERR;
    }

    if (CreateUbsShmRegion(g_region_name) != UBRING_OK) {
        LOG(ERROR) << "Create Ubs region failed.";
        return UBRING_ERR;
    }

    if (InitShmTimer(&g_shm_list) != UBRING_OK) {
        LOG(ERROR) << "Ubs shm list init failed.";
        return UBRING_ERR;
    }

    LOG(INFO) << "Ubs shm init success.";
    return UBRING_OK;
}

RETURN_CODE UbsShmFini(void) {
    DeleteTimerSafe(g_shm_timer_fd);
    g_shm_timer_fd = 0;
    if (g_shm_list != nullptr) {
        UbsShmCallback(g_shm_list);
        if (g_shm_list->size != 0) {
            UbsShmAddTimer(g_shm_list);
            return UBRING_ERR;
        }
    }
    const int ret = ubsmem_finalize();
    if (ret != UBSM_OK) {
        LOG(ERROR) << "Ubs shm finalize fail, ret=" << ret;
        return UBRING_ERR;
    }
    if (g_shm_list != nullptr) {
        DestroyShmTimer(g_shm_list);
        g_shm_list = nullptr;
    }
    return UBRING_OK;
}

void *UbsShmCallback(void* args) {
    auto* list = static_cast<ShmList*>(args);
    if (list == nullptr) {
        return nullptr;
    }
    LOCK_GUARD(list->shm_lock);
    for (ShmListNode* task = list->head; task != nullptr;) {
        ShmListNode* next = task->next;
        if (TryUbsCleanup(task)) {
            if (task->prev != nullptr) {
                task->prev->next = next;
            } else {
                list->head = next;
            }
            if (next != nullptr) {
                next->prev = task->prev;
            } else {
                list->tail = task->prev;
            }
            --list->size;
            free(task);
        }
        task = next;
    }
    return nullptr;
}

RETURN_CODE UbsShmAddTimer(ShmList *shm_list)
{
    const uint32_t timer_interval_s = FLAGS_ub_flying_io_timeout_s;
    itimerspec time_spec = {
        .it_interval = {.tv_sec = timer_interval_s, .tv_nsec = 0},
        .it_value = {.tv_sec = 0, .tv_nsec = 1}
    };
    int timer_fd = TimerStart(&time_spec, UbsShmCallback, (void*)shm_list);
    if (UNLIKELY(timer_fd == -1)) {
        LOG(ERROR) << "Start shm timer failed.";
        return UBRING_ERR;
    }
    g_shm_timer_fd = timer_fd;

    return UBRING_OK;
}

RETURN_CODE InitShmTimer(ShmList **shm_list)
{
    *shm_list = (ShmList *)malloc(sizeof(ShmList));
    if (*shm_list == nullptr) {
        LOG(ERROR) << "Malloc shm list failed.";
        return UBRING_ERR;
    }
    (*shm_list)->head = nullptr;
    (*shm_list)->tail = nullptr;
    (*shm_list)->size = 0;

    if (pthread_mutex_init(&(*shm_list)->shm_lock, nullptr) != 0) {
        LOG(ERROR) << "Init shm list mutex failed.";
        FREE_PTR(*shm_list);
        return UBRING_ERR;
    }

    if (UbsShmAddTimer(*shm_list) == UBRING_ERR) {
        LOG(ERROR) << "Ubs add timer failed.";
        pthread_mutex_destroy(&(*shm_list)->shm_lock);
        FREE_PTR(*shm_list);
        return UBRING_ERR;
    }
    return UBRING_OK;
}

RETURN_CODE DestroyShmTimer(ShmList *shm_list)
{
    DeleteTimerSafe((uint32_t)g_shm_timer_fd);
    g_shm_timer_fd = 0;
    if (shm_list == nullptr) {
        LOG(WARNING) << "Shm list is null.";
        return UBRING_ERR;
    }
    ShmListNode* current = shm_list->head;
    ShmListNode* next;

    while (current != nullptr) {
        next = current->next;
        free(current);
        current = next;
    }
    pthread_mutex_destroy(&shm_list->shm_lock);
    FREE_PTR(shm_list);
    return UBRING_OK;
}

}
}
