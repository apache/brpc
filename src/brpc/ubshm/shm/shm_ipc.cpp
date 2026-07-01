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
#include "brpc/ubshm/common/common.h"
#include "brpc/ubshm/shm/shm_def.h"
#include "brpc/ubshm/shm/shm_ipc.h"

namespace brpc {
namespace ubring {
namespace {

RETURN_CODE ReserveIpcShm(int fd, const SHM *shm)
{
#if defined(__linux__)
    const int rc = posix_fallocate(fd, 0, (off_t)shm->len);
    if (rc != 0) {
        LOG(ERROR) << "IPC reserve shm=" << shm->name << " length=" << shm->len
                   << " failed, ret(" << rc << ").";
        return SHM_ERR;
    }
#else
    UNREFERENCE_PARAM(fd);
    UNREFERENCE_PARAM(shm);
#endif
    return UBRING_OK;
}

RETURN_CODE CheckIpcShmSize(int fd, const SHM *shm)
{
    struct stat st;
    if (fstat(fd, &st) != 0) {
        LOG(ERROR) << "IPC stat shm=" << shm->name << " failed, ret(" << errno << ").";
        return SHM_ERR;
    }
    if ((uint64_t)st.st_size < (uint64_t)shm->len) {
        LOG(ERROR) << "IPC shm=" << shm->name << " actual length=" << st.st_size
                   << " is shorter than requested length=" << shm->len << ".";
        return SHM_ERR;
    }
    return UBRING_OK;
}

}  // namespace

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

    if (ReserveIpcShm(fd, shm) != UBRING_OK) {
        close(fd);
        shm_unlink(shm->name);
        return SHM_ERR;
    }

    shm->addr = (uint8_t*)mmap(NULL, shm->len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm->addr == (uint8_t*)MAP_FAILED) {
        LOG(ERROR) << "IPC map shm=" << shm->name << " length=" << shm->len << " failed, ret(" << errno << ").";
        shm->addr = NULL;
        close(fd);
        shm_unlink(shm->name);
        return SHM_ERR;
    }

    close(fd);
    return UBRING_OK;
}

RETURN_CODE IpcShmMunmap(SHM *shm)
{
    if (shm->addr == NULL) {
        LOG(INFO) << "IPC unmap shm=" << shm->name << " already unmapped.";
        return UBRING_OK;
    }

    int ret = munmap(shm->addr, shm->len);
    if (ret != UBRING_OK) {
        LOG(ERROR) << "IPC unmap shm=" << shm->name << " failed, errno=" << errno;
        return SHM_ERR;
    }

    shm->addr = NULL;
    LOG(INFO) << "IPC unmap shm=" << shm->name << " length=" << shm->len << " success.";
    return UBRING_OK;
}

RETURN_CODE IpcShmFree(SHM *shm)
{
    // free
    int ret = shm_unlink(shm->name);
    if (ret != UBRING_OK) {
        if (errno == EBUSY) {
            LOG_EVERY_SECOND(ERROR) << "IPC free shm=" << shm->name << " failed, errno=" << errno;
            return SHM_ERR_RESOURCE_ATTACHED;
        }
        if (errno == ENOENT) {
            LOG(INFO) << "IPC free shm=" << shm->name << " already deleted.";
            shm->addr = NULL;
            return SHM_ERR_NOT_FOUND;
        }
        LOG_EVERY_SECOND(ERROR) << "IPC free shm=" << shm->name << " failed, errno=" << errno;
        return SHM_ERR;
    }
    return UBRING_OK;
}

RETURN_CODE IpcShmLocalFree(SHM *shm)
{
    if (shm->addr == NULL) {
        LOG(INFO) << "IPC free local shm=" << shm->name << " already freed.";
        return SHM_ERR_NOT_FOUND;
    }

    int ret = munmap(shm->addr, shm->len);
    if (ret != UBRING_OK) {
        LOG(WARNING) << "IPC unmap shm=" << shm->name << " failed, ret=" << ret;
    } else {
        shm->addr = NULL;
    }

    ret = shm_unlink(shm->name);
    if (ret != UBRING_OK) {
        if (errno == EBUSY) {
            LOG_EVERY_SECOND(ERROR) << "IPC delete shm=" << shm->name << " failed, ret=" << ret;
            return SHM_ERR_RESOURCE_ATTACHED;
        }
        if (errno == ENOENT) {
            LOG(INFO) << "IPC delete shm=" << shm->name << " already deleted by peer.";
            shm->addr = NULL;
            return SHM_ERR_NOT_FOUND;
        }
        LOG_EVERY_SECOND(ERROR) << "IPC delete shm=" << shm->name << " failed, ret=" << ret;
        return SHM_ERR;
    }
    shm->addr = NULL;
    LOG(INFO) << "IPC free local shm=" << shm->name << " success.";
    return UBRING_OK;
}

RETURN_CODE IpcShmRemoteMalloc(SHM *shm)
{
    int fd = shm_open(shm->name, O_RDWR, SHM_IPC_MODE);
    if (fd < 0) {
        LOG(ERROR) << "IPC open shm=" << shm->name << " failed, ret=" << errno;
        return SHM_ERR;
    }

    if (CheckIpcShmSize(fd, shm) != UBRING_OK) {
        close(fd);
        return SHM_ERR;
    }

    shm->addr = (uint8_t*)mmap(NULL, shm->len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm->addr == (uint8_t*)MAP_FAILED) {
        LOG(ERROR) << "IPC map shm=" << shm->name << " failed, ret=" << errno;
        shm->addr = NULL;
        close(fd);
        return SHM_ERR;
    }

    close(fd);
    return UBRING_OK;
}

RETURN_CODE IpcShmLocalMmap(SHM *shm, int prot)
{
    int fd = shm_open(shm->name, O_RDWR, SHM_IPC_MODE);
    if (fd < 0) {
        LOG(ERROR) << "IPC open shm=" << shm->name << " failed, ret=" << errno;
        return SHM_ERR;
    }

    if (CheckIpcShmSize(fd, shm) != UBRING_OK) {
        close(fd);
        return SHM_ERR;
    }

    shm->addr = (uint8_t*)mmap(NULL, shm->len, prot, MAP_SHARED, fd, 0);
    if (shm->addr == (uint8_t*)MAP_FAILED) {
        LOG(ERROR) << "IPC map shm=" << shm->name << " failed, ret=" << errno;
        shm->addr = NULL;
        close(fd);
        return SHM_ERR;
    }

    close(fd);
    LOG(INFO) << "IPC mmap remote shm=" << shm->name << " length=" << shm->len << " success.";
    return UBRING_OK;
}

RETURN_CODE IpcShmRemoteFree(SHM *shm)
{
    if (shm->addr == NULL) {
        LOG(INFO) << "IPC free remote shm=" << shm->name << " already freed.";
        return UBRING_OK;
    }

    int ret = munmap(shm->addr, shm->len);
    if (ret != UBRING_OK) {
        LOG(ERROR) << "IPC unmap shm=" << shm->name << " failed, ret=" << ret;
        return SHM_ERR;
    }

    shm->addr = NULL;
    LOG(INFO) << "IPC free remote shm=" << shm->name << " success.";
    return UBRING_OK;
}
}
}
