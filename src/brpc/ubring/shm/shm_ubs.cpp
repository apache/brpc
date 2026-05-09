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
#include <gflags/gflags.h>
#include "brpc/ubring/timer/timer_mgr.h"
#include "brpc/ubring/common/thread_lock.h"
#include "brpc/ubring/common/common.h"
#include "brpc/ubring/shm/shm_def.h"
#include "brpc/ubring/ub_ring_manager.h"
#include "brpc/ubring/rack_mem/ubs_mem.h"
#include "brpc/ubring/rack_mem/ubs_mem_def.h"
#ifdef UT
#include "ubs_mem.h"
#endif
#include "shm_ubs.h"

namespace brpc {
namespace ubring {
#define UBRING_MK_UBSM(ret, fn, args) ret (*fn) args = NULL
#include "brpc/ubring/rack_mem/declare_shm_ubs.h"
#define SHM_RIGHT_MODE 0666
#define UBRING_REGION_NAME_PREFIX "UbrONE2ALLRegion"
DEFINE_uint32(node_location, 1, "Location of the ub machine.");
DEFINE_bool(shm_wr_delay_comp, true, "Indicates whether to enable the write relay."
            "0: relay; 1: non-relay.");
DEFINE_int32(ub_flying_io_timeout, 5, "Waiting time for stopping data"
            "sending and receiving when the link is disconnected.");
char g_regionName[MAX_REGION_NAME_DESC_LENGTH] = {0};
int g_shmTimerFd = 0;
ShmList *g_shmList = NULL;
static RETURN_CODE UbsShmInterfacesLoad(void);
char hostname[MAX_HOST_NAME_DESC_LENGTH];

RETURN_CODE UbsShmInterfacesLoad(void)
{
#ifndef UT
    const char *ubsmSdkLocation = "/usr/local/ubs_mem/lib/libubsm_sdk.so";
#if defined(OS_LINUX)
    void* dlhandler = dlmopen(LM_ID_NEWLM, ubsmSdkLocation, RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE | RTLD_DEEPBIND);
#elif defined(OS_MACOSX)
    void* dlhandler = dlopen(ubsmSdkLocation, RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
#endif
    if (dlhandler == NULL) {
        LOG(ERROR) << "Dlopen libubsm_sdk.so in " << ubsmSdkLocation << " failed, error:" << dlerror();
        return UBRING_ERR;
    }

#define UBRING_MK_UBSM_OPTIONAL(ret, fn, args)                                          \
    do {                                                                             \
        fn = (decltype(fn))dlsym(dlhandler, #fn);                                                  \
    } while (0)

#define UBRING_MK_UBSM(ret, fn, args)                                                   \
    do {                                                                             \
        if ((fn) != NULL) {                                                          \
            break;                                                                   \
        }                                                                            \
        UBRING_MK_UBSM_OPTIONAL(ret, fn, args);                                         \
        if ((fn) == NULL) {                                                          \
            LOG(ERROR) << "Fail load ubs_mem func " << #fn <<" error:" << dlerror(); \
            return UBRING_ERR;                                                          \
        }                                                                            \
    } while (0)
#include "brpc/ubring/rack_mem/declare_shm_ubs.h"

    dlclose(dlhandler);
    dlhandler = NULL;
#endif
    return UBRING_OK;
}

static RETURN_CODE CreateUbsShmRegion(const char *regionName)
{
    int ret = snprintf(g_regionName, MAX_REGION_NAME_DESC_LENGTH, "%s_%u",
        UBRING_REGION_NAME_PREFIX, FLAGS_node_location);
    if (ret < 0) {
        LOG(ERROR) << "Snprintf_s region name failed, ret=" << ret;
        return UBRING_ERR;
    }

    ubsmem_regions_t regions = {0}; // 16 * (48 + 1) bytes, 约0.8k
    ret = ubsmem_lookup_regions(&regions);
    if (ret != UBSM_OK || regions.region[0].host_num <= 0) {
        LOG(ERROR) << "Ubs lookup share region failed, ret=" << ret << ", region.num=" << regions.region[0].host_num;
        return UBRING_ERR;
    }
    ubsmem_region_attributes_t regionAttr = {0};
    regionAttr.host_num = regions.region[0].host_num;
    for (int i = 0; i < regionAttr.host_num; i++) {
        strcpy(regionAttr.hosts[i].host_name, regions.region[0].hosts[i].host_name);
        regionAttr.hosts[i].affinity = (strcmp(regionAttr.hosts[i].host_name, hostname) == 0) ?
            true : false;
    }

    ret = ubsmem_create_region(regionName, 0, &regionAttr);
    if (ret == UBSM_ERR_ALREADY_EXIST) {
        LOG(WARNING) << "Ubs region exists, region_name=" << regionName;
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

RETURN_CODE UbsShmLocalMalloc(SHM *shm)
{
    int ret = ubsmem_shmem_allocate(g_regionName, shm->name, shm->len, SHM_RIGHT_MODE,
        AquireFlagIfWrDelayComp(UBSM_FLAG_ONLY_IMPORT_NONCACHE | UBSM_FLAG_MEM_ANONYMOUS));
do {
    if (ret == UBSM_ERR_ALREADY_EXIST) {
        if (ubsmem_shmem_deallocate(shm->name) != UBSM_OK) {
            LOG(ERROR) << "Ubs create shm name=" << shm->name << " failed, shm exists, ret=" << ret;
            return SHM_ERR_EXIST;
        }
        LOG(INFO) << "Ubs delete shm name=" << shm->name << " success, try to recreate.";
        ret = ubsmem_shmem_allocate(g_regionName, shm->name, shm->len, SHM_RIGHT_MODE,
            AquireFlagIfWrDelayComp(UBSM_FLAG_ONLY_IMPORT_NONCACHE | UBSM_FLAG_MEM_ANONYMOUS));
        if (ret != UBSM_OK) {
            LOG(ERROR) << "Ubs recreate shm name=" << shm->name << " failed, ret=" << ret;
            return SHM_ERR;
        }
    } else if (ret != UBSM_OK) {
        LOG(ERROR) << "Ubs create shm name=" << shm->name << " failed, ret=" << ret;
        return SHM_ERR;
    }
} while (0);

    ret = ubsmem_shmem_map(NULL, shm->len, PROT_READ | PROT_WRITE, MAP_SHARED, shm->name, 0, (void**)&(shm->addr));
    if (ret != UBSM_OK) {
        LOG(ERROR) << "Ubs map shm=" << shm->name << " failed, ret=" << ret;
        if (ret == UBSM_ERR_NOT_FOUND) {
            return SHM_ERR_NOT_FOUND;
        }
        ubsmem_shmem_deallocate(shm->name);
        return SHM_ERR;
    }

    // 通过MXE获取memid
    shm->memid = 1; // 暂时打桩
    LOG(INFO) << "Ubs malloc local shm=" << shm->name << " length=" << shm->len << " memid=" << shm->memid << " success.";
    return UBRING_OK;
}

RETURN_CODE UbsShmMunmap(SHM *shm)
{
    // unmap
    if (shm->addr == NULL) {
        LOG(ERROR) << "Ubs input shm param is invalid, addr is NULL.";
        return SHM_ERR_INPUT_INVALID;
    }

    int ret = ubsmem_shmem_unmap(shm->addr, shm->len);
    if (ret != UBSM_OK) {
        if (ret == UBSM_ERR_NET) {
            LOG(ERROR) << "Ubs unmap shm=" << shm->name << " failed, ubsm net err=" << ret;
            AddShmToList(g_shmList, shm);
            return SHM_ERR_UBSM_NET_ERR;
        }
        LOG(ERROR) << "Ubs unmap shm=" << shm->name << " length=" << shm->len << " failed, ret=" << ret;
        return SHM_ERR;
    }

    LOG(INFO) << "Ubs unmap shm=" << shm->name << " length=" << shm->len << " success.";
    return UBRING_OK;
}

RETURN_CODE UbsShmFree(SHM *shm)
{
    if (shm->addr == NULL) {
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
    shm->addr = NULL;
    LOG(INFO) << "Ubs free shm=" << shm->name << " length=" << shm->len << " success.";
    return UBRING_OK;
}

RETURN_CODE UbsShmLocalFree(SHM *shm)
{
    // unmap
    if (shm->addr == NULL) {
        LOG(ERROR) << "Ubs input shm param is invalid, addr is NULL.";
        return SHM_ERR_INPUT_INVALID;
    }

    int ret = ubsmem_shmem_unmap(shm->addr, shm->len);
    if (ret != UBSM_OK) {
        if (ret == UBSM_ERR_NET) {
            LOG(ERROR) << "Ubs unmap shm=" << shm->name << " failed, ubsm net err=" << ret;
            AddShmToList(g_shmList, shm);
            return SHM_ERR_UBSM_NET_ERR;
        }
        LOG(WARNING) << "Ubs unmap shm=" << shm->name << " length=" << shm->len << " failed, ret=" << ret;
    }

    // free
    ret = ubsmem_shmem_deallocate(shm->name);
    if (ret != UBSM_OK) {
        if (ret == UBSM_ERR_IN_USING) {
            LOG_EVERY_SECOND(INFO) << "Ubs delete shm=" << shm->name << " failed, resource attached=" << ret;
            return SHM_ERR_RESOURCE_ATTACHED;
        }
        LOG(ERROR) << "Ubs delete shm=" << shm->name << " failed, ret=" << ret;
        return SHM_ERR;
    }
    shm->addr = NULL;
    LOG(INFO) << "Ubs free local shm=" << shm->name << " length=" << shm->len << " success.";
    return UBRING_OK;
}

RETURN_CODE UbsShmRemoteMalloc(SHM *shm)
{
    int ret = ubsmem_shmem_map(NULL, shm->len, PROT_READ | PROT_WRITE, MAP_SHARED, shm->name, 0, (void**)&(shm->addr));
    if (ret != UBSM_OK) {
        LOG(ERROR) << "Ubs map Shm=" << shm->name << " failed, ret=" << ret;
        return SHM_ERR;
    }

    LOG(INFO) << "Ubs malloc remote shm=" << shm->name << " length=" << shm->len << " success.";
    return UBRING_OK;
}

RETURN_CODE UbsShmLocalMmap(SHM *shm, int prot)
{
    int ret = ubsmem_shmem_map(NULL, shm->len, prot, MAP_SHARED, shm->name, 0, (void**)&(shm->addr));
    if (ret != UBSM_OK) {
        LOG(ERROR) << "Ubs map Shm=" << shm->name << " failed, ret=" << ret;
        return SHM_ERR;
    }

    LOG(INFO) << "Ubs mmap remote shm=" << shm->name << " length=" << shm->len << " success.";
    return UBRING_OK;
}

RETURN_CODE UbsShmRemoteFree(SHM *shm)
{
    // unmap
    if (shm->addr == NULL) {
        LOG(ERROR) << "Ubs input shm param is invalid, addr is NULL.";
        return SHM_ERR_INPUT_INVALID;
    }

    int ret = ubsmem_shmem_unmap(shm->addr, shm->len);
    if (ret != UBSM_OK) {
        if (ret == UBSM_ERR_NET) {
            LOG(ERROR) << "Ubs unmap shm=" << shm->name << " failed, ubsm net err=" << ret;
            AddShmToList(g_shmList, shm);
            return SHM_ERR_UBSM_NET_ERR;
        }
        LOG(ERROR) << "Ubs unmap shm=" << shm->name << " length=" << shm->len << " failed, ret=" << ret;
        return SHM_ERR;
    }

    LOG(INFO) << "Ubs free Remote shm=" << shm->name << " length=" << shm->len << " success.";
    return UBRING_OK;
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
    // 加载libubsm_sdk.so函数指针
    RETURN_CODE retCode = UbsShmInterfacesLoad();
    if (retCode != UBRING_OK) {
        LOG(ERROR) << "Load ubs shm functions failed, ret=" << retCode;
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

    if (CreateUbsShmRegion(g_regionName) != UBRING_OK) {
        LOG(ERROR) << "Create Ubs region failed.";
        return UBRING_ERR;
    }

    if (InitShmTimer(&g_shmList) != UBRING_OK) {
        LOG(ERROR) << "Ubs shm list init failed.";
        return UBRING_ERR;
    }

    LOG(INFO) << "Ubs shm init success.";
    return UBRING_OK;
}

RETURN_CODE UbsShmFini(void)
{
    int ret = ubsmem_finalize();
    if (ret != UBSM_OK) {
        LOG(ERROR) << "Ubs shm finalize fail, ret=" << ret;
        return UBRING_ERR;
    }

    if (UNLIKELY(DestroyShmTimer(g_shmList) != UBRING_OK)) {
        LOG(ERROR) << "Ubs shm list finalize failed.";
        return UBRING_ERR;
    }

    LOG(INFO) << "Ubs shm finalize success.";
    return UBRING_OK;
}

static void DeleteShmToList(ShmList* shmList)
{
    if (shmList == NULL || shmList->head == NULL) {
        return;
    }

    ShmListNode *curNode = shmList->head;
    shmList->head = curNode->next;
    if (shmList->head != NULL) {
        shmList->head->prev = NULL;
    } else {
        shmList->tail = NULL;
    }
    LOG(INFO) << "Delete shm to list, name=" << curNode->shm.name << " size=" << shmList->size;
    FREE_PTR(curNode);
    shmList->size--;
}

void *UbsShmCallback(void* args)
{
    ShmList *shmList = (ShmList*)args;
    if (UNLIKELY(shmList == NULL)) {
        LOG(ERROR) << "Shm list is null.";
        return NULL;
    }

    LOCK_GUARD(shmList->shmLock);
    while (shmList->head != NULL) {
        SHM shm = shmList->head->shm;
        if (shm.addr == NULL) {
            LOG(ERROR) << "Ubs input shm param is invalid, addr is NULL.";
            return NULL;
        }

        int ret = ubsmem_shmem_unmap(shm.addr, shm.len);
        if (ret != UBSM_OK) {
            if (ret == UBSM_ERR_NET) {
                return NULL;
            }
            LOG(ERROR) << "Ubs unmap shm=" << shm.name << " length=" << shm.len << " failed, ret=" << ret;
            return NULL;
        }
        LOG(INFO) << "Ubs unmap shm=" << shm.name << " length=" << shm.len << " success.";

        ret = ubsmem_shmem_deallocate(shm.name);
        if (ret != UBSM_OK) {
            DeleteShmToList(shmList);
            LOG(ERROR) << "Ubs delete shm=" << shm.name << " failed, ret=" << ret;
            return NULL;
        }
        DeleteShmToList(shmList);
        LOG(INFO) << "Ubs free local shm=" << shm.name << " length=" << shm.len << " success.";
    }

    return NULL;
}

RETURN_CODE UbsShmAddTimer(ShmList *shmList)
{
    uint32_t timerInterval = FLAGS_ub_flying_io_timeout;
    itimerspec timeSpec = {
        .it_interval = {.tv_sec = timerInterval, .tv_nsec = 0},
        .it_value = {.tv_sec = 0, .tv_nsec = 1}
    };
    int timerFd = TimerStart(&timeSpec, UbsShmCallback, (void*)shmList);
    if (UNLIKELY(timerFd == -1)) {
        LOG(ERROR) << "Start shm timer failed.";
        return UBRING_ERR;
    }
    g_shmTimerFd = timerFd;

    return UBRING_OK;
}

RETURN_CODE InitShmTimer(ShmList **shmList)
{
    *shmList = (ShmList *)malloc(sizeof(ShmList));
    if (*shmList == NULL) {
        LOG(ERROR) << "Malloc shm list failed.";
        return UBRING_ERR;
    }
    (*shmList)->head = NULL;
    (*shmList)->tail = NULL;
    (*shmList)->size = 0;

    if (pthread_mutex_init(&(*shmList)->shmLock, NULL) != 0) {
        LOG(ERROR) << "Init shm list mutex failed.";
        FREE_PTR(*shmList);
        return UBRING_ERR;
    }

    if (UbsShmAddTimer(*shmList) == UBRING_ERR) {
        LOG(ERROR) << "Ubs add timer failed.";
        FREE_PTR(*shmList);
        return UBRING_ERR;
    }
    return UBRING_OK;
}

RETURN_CODE DestroyShmTimer(ShmList *shmList)
{
    DeleteTimerSafe((uint32_t)g_shmTimerFd);
    if (shmList == NULL) {
        LOG(WARNING) << "Shm list is null.";
        return UBRING_ERR;
    }
    ShmListNode* current = shmList->head;
    ShmListNode* next;

    while (current != NULL) {
        next = current->next;
        free(current);
        current = next;
    }
    pthread_mutex_destroy(&shmList->shmLock);
    FREE_PTR(shmList);
    return UBRING_OK;
}

RETURN_CODE IsExistInShmList(ShmList *shmList, const SHM *shm)
{
    LOCK_GUARD(shmList->shmLock);
    if (UNLIKELY(shmList == NULL)) {
        LOG(ERROR) << "Shm list is null.";
        return UBRING_ERR;
    }

    ShmListNode *curNode = shmList->head;
    while (curNode != NULL) {
        if (strcmp(curNode->shm.name, shm->name) == 0 && curNode->shm.len == shm->len) {
            return UBRING_OK;
        }
        curNode = curNode->next;
    }
    return UBRING_ERR;
}

RETURN_CODE AddShmToList(ShmList *shmList, SHM *shm)
{
    if (shmList == NULL || shm == NULL) {
        LOG(ERROR) << "Shm list or shm is null.";
        return UBRING_ERR;
    }

    if (IsExistInShmList(shmList, shm) == UBRING_OK) {
        LOG(ERROR) << "Shm name=" << shm->name << " is exist in shm list.";
        return UBRING_ERR;
    }

    ShmListNode *newShmNode = (ShmListNode *)malloc(sizeof(ShmListNode));
    if (newShmNode == NULL) {
        LOG(ERROR) << "Malloc shm node failed.";
        return UBRING_ERR;
    }

    memcpy(&newShmNode->shm, shm, sizeof(SHM));
    LOCK_GUARD(shmList->shmLock);
    newShmNode->next = NULL;
    newShmNode->prev = shmList->tail;
    if (shmList->tail) {
        shmList->tail->next = newShmNode;
        shmList->tail = newShmNode;
    } else {
        shmList->head = newShmNode;
        shmList->tail = newShmNode;
    }
    shmList->size++;
    LOG(INFO) << "Add shm to list success, shm name=" << shm->name << " size=" << shmList->size;
    return UBRING_OK;
}
}
}