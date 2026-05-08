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

#include <gflags/gflags.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "brpc/ubring/common/common.h"
#include "brpc/ubring/shm/shm_ipc.h"
#include "brpc/ubring/shm/shm_ubs.h"
#include "brpc/ubring/shm/shm_mgr.h"

namespace brpc {
namespace ubring {
DEFINE_int32(ub_shm_type, 1, "shm type: 1-ipc; 2-ub_ring");
static SHM_TYPE g_shmType;

static bool CheckInputShmParam(SHM *shm) {
    if (shm == NULL) {
        LOG(ERROR) << "Input Param shm is NULL.";
        return false;
    }

    size_t nameLen = strlen(shm->name);
    if (nameLen <= 0 || nameLen > SHM_MAX_NAME_LEN) {
        LOG(ERROR) << "Shm name=" << shm->name << ", length=" << shm->len 
                   << ", which is not between 1 and " << SHM_MAX_NAME_LEN;
        return false;
    }

    if (shm->len <= 0) {
        LOG(ERROR) << "Shm length=" << shm->len << " is invalid.";
        return false;
    }

    if (shm->len < SHM_ALLOC_UNIT_SIZE || (shm->len & (SHM_ALLOC_UNIT_SIZE - 1)) != 0) {
        LOG(ERROR) << "Shm length=" << shm->len << " need to be (1..n) * 4MB.";
        return false;
    }

    return true;
}

RETURN_CODE ShmMgrInit(void) {
    if (UNLIKELY(FLAGS_ub_shm_type >= (uint32_t)SHM_TYPE_UNSUPPORT)) {
        LOG(ERROR) << "Shm type config=" << FLAGS_ub_shm_type << " is not supported.";
        return UBRING_ERR;
    }

    g_shmType = (SHM_TYPE)FLAGS_ub_shm_type;
    if (g_shmType == SHM_TYPE_UBS) {
        if (UbsShmInit() != UBRING_OK) {
            LOG(ERROR) << "Init beiming ubs shm failed.";
            return UBRING_ERR;
        }
    }
    LOG(INFO) << "shm mgr init success, shm type=" << g_shmType;
    return UBRING_OK;
}

void ShmMgrFini(void) {
    if (g_shmType == SHM_TYPE_UBS) {
        if (UbsShmFini() != UBRING_OK) {
            LOG(ERROR) << "Fini beiming ubs shm failed.";
            return;
        }
    }
    LOG(INFO) << "shm mgr fini success, shm type=" << g_shmType;
}

void SetShmType(SHM_TYPE type) {
    g_shmType = type;
}

RETURN_CODE ShmLocalMalloc(SHM *shm) {
    if (UNLIKELY(!CheckInputShmParam(shm))) {
        LOG(ERROR) << "Input param shm is invalid.";
        return SHM_ERR_INPUT_INVALID;
    }

    RETURN_CODE rc = UBRING_OK;
    switch (g_shmType) {
        case SHM_TYPE_IPC:
            rc = IpcShmLocalMalloc(shm);
            break;
        case SHM_TYPE_UBS:
            rc = UbsShmLocalMalloc(shm);
            break;
        default:
            rc = SHM_ERR;
            LOG(ERROR) << "Unsupported shm type.";
    }
    return rc;
}

RETURN_CODE ShmLocalCalloc(SHM *shm) {
    RETURN_CODE rc = ShmLocalMalloc(shm);
    if (UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Failed to alloc local shm.";
        return rc;
    }
    memset(shm->addr, 0, shm->len);
    return UBRING_OK;
}

RETURN_CODE ShmLocalFree(SHM *shm) {
    if (UNLIKELY(!CheckInputShmParam(shm))) {
        LOG(ERROR) << "Input param shm is invalid.";
        return SHM_ERR_INPUT_INVALID;
    }

    RETURN_CODE rc = UBRING_OK;
    switch (g_shmType) {
        case SHM_TYPE_IPC:
            rc = IpcShmLocalFree(shm);
            break;
        case SHM_TYPE_UBS:
            rc = UbsShmLocalFree(shm);
            break;
        default:
            rc = SHM_ERR;
            LOG(ERROR) << "Unsupported shm type.";
    }
    return rc;
}

RETURN_CODE ShmRemoteMalloc(SHM *shm) {
    if (UNLIKELY(!CheckInputShmParam(shm))) {
        LOG(ERROR) << "Input param shm is invalid.";
        return SHM_ERR_INPUT_INVALID;
    }

    RETURN_CODE rc = UBRING_OK;
    switch (g_shmType) {
        case SHM_TYPE_IPC:
            rc = IpcShmRemoteMalloc(shm);
            break;
        case SHM_TYPE_UBS:
            rc = UbsShmRemoteMalloc(shm);
            break;
        default:
            rc = SHM_ERR;
            LOG(ERROR) << "Unsupported shm type.";
    }
    return rc;
}

RETURN_CODE ShmRemoteFree(SHM *shm) {
    if (UNLIKELY(!CheckInputShmParam(shm))) {
        LOG(ERROR) << "Input param shm is invalid.";
        return SHM_ERR_INPUT_INVALID;
    }

    RETURN_CODE rc = UBRING_OK;
    switch (g_shmType) {
        case SHM_TYPE_IPC:
            rc = IpcShmRemoteFree(shm);
            break;
        case SHM_TYPE_UBS:
            rc = UbsShmRemoteFree(shm);
            break;
        default:
            rc = SHM_ERR;
            LOG(ERROR) << "Unsupported shm type.";
    }
    return rc;
}

RETURN_CODE ShmLocalMmap(SHM *shm, int prot) {
    if (UNLIKELY(!CheckInputShmParam(shm))) {
        LOG(ERROR) << "Input param shm is invalid.";
        return SHM_ERR_INPUT_INVALID;
    }

    RETURN_CODE rc = UBRING_OK;
    switch (g_shmType) {
        case SHM_TYPE_IPC:
            rc = IpcShmLocalMmap(shm, prot);
            break;
        case SHM_TYPE_UBS:
            rc = UbsShmLocalMmap(shm, prot);
            break;
        default:
            rc = SHM_ERR;
            LOG(ERROR) << "Unsupported shm type.";
    }
    return rc;
}

RETURN_CODE ShmMunmap(SHM *shm) {
    if (UNLIKELY(!CheckInputShmParam(shm))) {
        LOG(ERROR) << "Input param shm is invalid.";
        return SHM_ERR_INPUT_INVALID;
    }

    RETURN_CODE rc = UBRING_OK;
    switch (g_shmType) {
        case SHM_TYPE_IPC:
            rc = IpcShmMunmap(shm);
            break;
        case SHM_TYPE_UBS:
            rc = UbsShmMunmap(shm);
            break;
        default:
            rc = SHM_ERR;
            LOG(ERROR) << "Unsupported shm type.";
    }
    return rc;
}

RETURN_CODE ShmFree(SHM *shm) {
    if (UNLIKELY(!CheckInputShmParam(shm))) {
        LOG(ERROR) << "Input param shm is invalid.";
        return SHM_ERR_INPUT_INVALID;
    }

    RETURN_CODE rc = UBRING_OK;
    switch (g_shmType) {
        case SHM_TYPE_IPC:
            rc = IpcShmFree(shm);
            break;
        case SHM_TYPE_UBS:
            rc = UbsShmFree(shm);
            break;
        default:
            rc = SHM_ERR;
            LOG(ERROR) << "Unsupported shm type.";
    }
    return rc;
}
}
}