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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "brpc/ub/common/common.h"
#include "brpc/ub/shm/shm_def.h"
#include "brpc/ub/shm/shm_ipc.h"

namespace brpc {
namespace ub {
RETURN_CODE IpcShmLocalMalloc(SHM *shm)
{
    int fd = shm_open(shm->name, O_CREAT | O_EXCL | O_RDWR, SHM_IPC_MODE);
    if (fd < 0) {
        if (errno == EEXIST) {
            LOG(ERROR) << "IPC Create shm=" << shm->name << " failed, shm exists.";
            return SHM_ERR_EXIST;
        }

        LOG(ERROR) << "IPC Open shm=" << shm->name << " failed, ret(" << errno << ").";
        return SHM_ERR;
    }

    int ret = ftruncate(fd, (off_t)shm->len);
    if (ret < 0) {
        LOG(ERROR) << "IPC Set shm=" << shm->name << " length=" << shm->len << " failed, ret(" << errno << ").";
        close(fd);
        shm_unlink(shm->name);
        return SHM_ERR;
    }

    shm->addr = (uint8_t*)mmap(NULL, shm->len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm->addr == (uint8_t*)MAP_FAILED) {
        LOG(ERROR) << "IPC map shm=" << shm->name << " length=" << shm->len << " failed, ret(" << errno << ").";
        close(fd);
        shm_unlink(shm->name);
        return SHM_ERR;
    }

    close(fd);
    LOG(DEBUG) << "IPC Create shm=" << shm->name << " length=" << shm->len << " success.";
    return HLC_OK;
}

RETURN_CODE IpcShmMunmap(SHM *shm)
{
    if (shm->addr == NULL) {
        LOG(ERROR) << "Input shm param is invalid, addr is NULL.";
        return SHM_ERR_INPUT_INVALID;
    }

    int ret = munmap(shm->addr, shm->len);
    if (ret != HLC_OK) {
        LOG(ERROR) << "IPC unmap shm=" << shm->name << " failed, errno=" << errno;
        return SHM_ERR;
    }

    LOG(DEBUG) << "IPC unmap shm=" << shm->name << " length=" << shm->len << " success.";
    return HLC_OK;
}

RETURN_CODE IpcShmFree(SHM *shm)
{
    if (shm->addr == NULL) {
        LOG(ERROR) << "Input shm param is invalid, addr is NULL.";
        return SHM_ERR_INPUT_INVALID;
    }

    // free
    int ret = shm_unlink(shm->name);
    if (ret != HLC_OK) {
        if (errno == EBUSY) {
            LOG_EVERY_SECOND(ERROR) << "IPC free shm=" << shm->name << " failed, errno=" << errno;
            return SHM_ERR_RESOURCE_ATTACHED;
        }
        LOG_EVERY_SECOND(ERROR) << "IPC free shm=" << shm->name << " failed, errno=" << errno;
        return SHM_ERR;
    }
    shm->addr = NULL;
    LOG(DEBUG) << "IPC free shm=" << shm->name << " success.";
    return HLC_OK;
}

RETURN_CODE IpcShmLocalFree(SHM *shm)
{
    if (shm->addr == NULL) {
        LOG(ERROR) << "Input shm param is invalid, addr is NULL.";
        return SHM_ERR_INPUT_INVALID;
    }

    int ret = munmap(shm->addr, shm->len);
    if (ret != HLC_OK) {
        LOG(WARNING) << "IPC unmap shm=" << shm->name << " failed, ret=" << ret;
    }

    ret = shm_unlink(shm->name);
    if (ret != HLC_OK) {
        if (errno == EBUSY) {
            LOG_EVERY_SECOND(ERROR) << "IPC delete shm=" << shm->name << " failed, ret=" << ret;
            return SHM_ERR_RESOURCE_ATTACHED;
        }
        LOG_EVERY_SECOND(ERROR) << "IPC delete shm=" << shm->name << " failed, ret=" << ret;
        return SHM_ERR;
    }
    shm->addr = NULL;
    LOG(DEBUG) << "IPC free local shm=" << shm->name << " success.";
    return HLC_OK;
}

RETURN_CODE IpcShmRemoteMalloc(SHM *shm)
{
    int fd = shm_open(shm->name, O_RDWR, SHM_IPC_MODE);
    if (fd < 0) {
        LOG(ERROR) << "IPC open shm=" << shm->name << " failed, ret=" << errno;
        return SHM_ERR;
    }

    shm->addr = (uint8_t*)mmap(NULL, shm->len, PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm->addr == (uint8_t*)MAP_FAILED) {
        LOG(ERROR) << "IPC map shm=" << shm->name << " failed, ret=" << errno;
        close(fd);
        return SHM_ERR;
    }

    close(fd);
    LOG(DEBUG) << "IPC malloc remote shm=" << shm->name << " length=" << shm->len << " success.";
    return HLC_OK;
}

RETURN_CODE IpcShmLocalMmap(SHM *shm, int prot)
{
    int fd = shm_open(shm->name, O_RDWR, SHM_IPC_MODE);
    if (fd < 0) {
        LOG(ERROR) << "IPC open shm=" << shm->name << " failed, ret=" << errno;
        return SHM_ERR;
    }

    shm->addr = (uint8_t*)mmap(NULL, shm->len, prot, MAP_SHARED, fd, 0);
    if (shm->addr == (uint8_t*)MAP_FAILED) {
        LOG(ERROR) << "IPC map shm=" << shm->name << " failed, ret=" << errno;
        close(fd);
        return SHM_ERR;
    }

    close(fd);
    LOG(DEBUG) << "IPC mmap remote shm=" << shm->name << " length=" << shm->len << " success.";
    return HLC_OK;
}

RETURN_CODE IpcShmRemoteFree(SHM *shm)
{
    if (shm->addr == NULL) {
        LOG(ERROR) << "Input shm param is invalid, addr is NULL.";
        return SHM_ERR_INPUT_INVALID;
    }

    int ret = munmap(shm->addr, shm->len);
    if (ret != HLC_OK) {
        LOG(ERROR) << "IPC unmap shm=" << shm->name << " failed, ret=" << ret;
        return SHM_ERR;
    }

    LOG(DEBUG) << "IPC free remote shm=" << shm->name << " success.";
    return HLC_OK;
}
}
}