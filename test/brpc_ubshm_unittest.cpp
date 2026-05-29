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

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <gtest/gtest.h>

#include "brpc/ubshm/common/common.h"
#include "brpc/ubshm/shm/shm_ipc.h"
#include "brpc/ubshm/shm/shm_mgr.h"
#include "brpc/ubshm/ub_ring.h"

namespace {

constexpr size_t kShmLen = SHM_ALLOC_UNIT_SIZE;

brpc::ubring::SHM MakeShm(const char* suffix, size_t len = kShmLen, bool unlink_existing = true) {
    brpc::ubring::SHM shm = {NULL, len, 0, {0}, 0};
    snprintf(shm.name, sizeof(shm.name), "/brpc_ut_%d_%s", (int)getpid(), suffix);
    if (unlink_existing) {
        brpc::ubring::IpcShmFree(&shm);
    }
    return shm;
}

void CleanupShm(brpc::ubring::SHM* shm) {
    if (shm == NULL) {
        return;
    }
    if (shm->addr != NULL) {
        brpc::ubring::IpcShmLocalFree(shm);
    } else {
        brpc::ubring::IpcShmFree(shm);
    }
}

}  // namespace

TEST(UBShmTest, IpcRemoteMallocRejectsShortObject) {
    brpc::ubring::SHM owner = MakeShm("short_owner", kShmLen);
    errno = 0;
    RETURN_CODE rc = brpc::ubring::IpcShmLocalMalloc(&owner);
    if (rc != UBRING_OK && errno == EPERM) {
        GTEST_SKIP() << "POSIX shm is not permitted in this environment.";
    }
    ASSERT_EQ(UBRING_OK, rc);

    brpc::ubring::SHM remote = MakeShm("short_owner", kShmLen * 2, false);
    rc = brpc::ubring::IpcShmRemoteMalloc(&remote);
    if (rc == UBRING_OK) {
        brpc::ubring::IpcShmRemoteFree(&remote);
    }
    EXPECT_NE(UBRING_OK, rc);
    EXPECT_EQ(NULL, remote.addr);

    CleanupShm(&owner);
}

TEST(UBShmTest, IpcMunmapClearsAddress) {
    brpc::ubring::SHM shm = MakeShm("munmap");
    errno = 0;
    RETURN_CODE rc = brpc::ubring::IpcShmLocalMalloc(&shm);
    if (rc != UBRING_OK && errno == EPERM) {
        GTEST_SKIP() << "POSIX shm is not permitted in this environment.";
    }
    ASSERT_EQ(UBRING_OK, rc);

    EXPECT_EQ(UBRING_OK, brpc::ubring::IpcShmMunmap(&shm));
    EXPECT_EQ(NULL, shm.addr);

    CleanupShm(&shm);
}

TEST(UBShmTest, IpcRemoteFreeClearsAddress) {
    brpc::ubring::SHM shm = MakeShm("remote_free");
    errno = 0;
    RETURN_CODE rc = brpc::ubring::IpcShmLocalMalloc(&shm);
    if (rc != UBRING_OK && errno == EPERM) {
        GTEST_SKIP() << "POSIX shm is not permitted in this environment.";
    }
    ASSERT_EQ(UBRING_OK, rc);

    EXPECT_EQ(UBRING_OK, brpc::ubring::IpcShmRemoteFree(&shm));
    EXPECT_EQ(NULL, shm.addr);

    CleanupShm(&shm);
}

TEST(UBShmTest, ServerAllocFailureReleasesMappedRemoteShm) {
    brpc::ubring::SetShmType(brpc::ubring::SHM_TYPE_IPC);

    brpc::ubring::SHM remote_owner = MakeShm("server_remote");
    errno = 0;
    RETURN_CODE rc = brpc::ubring::IpcShmLocalMalloc(&remote_owner);
    if (rc != UBRING_OK && errno == EPERM) {
        GTEST_SKIP() << "POSIX shm is not permitted in this environment.";
    }
    ASSERT_EQ(UBRING_OK, rc);
    brpc::ubring::SHM local_conflict = MakeShm("server_local");
    errno = 0;
    rc = brpc::ubring::IpcShmLocalMalloc(&local_conflict);
    if (rc != UBRING_OK && errno == EPERM) {
        CleanupShm(&remote_owner);
        GTEST_SKIP() << "POSIX shm is not permitted in this environment.";
    }
    ASSERT_EQ(UBRING_OK, rc);

    brpc::ubring::SHM remote_param = MakeShm("server_remote", kShmLen, false);
    brpc::ubring::SHM local_param = MakeShm("server_local", kShmLen, false);

    brpc::ubring::UBRing ring;
    EXPECT_EQ(-1, ring.UbrAllocateServerShm(&remote_param, &local_param));
    EXPECT_EQ(NULL, remote_param.addr);

    if (remote_param.addr != NULL) {
        brpc::ubring::IpcShmRemoteFree(&remote_param);
    }
    CleanupShm(&remote_owner);
    CleanupShm(&local_conflict);
}

TEST(UBShmTest, WritevRejectsPayloadLargerThanRingCapacity) {
    brpc::ubring::UbrDataStatusQMsg data_status = {};
    data_status.tail = 3;

    brpc::ubring::UbrTrx trx = {};
    trx.ubrTx.localDataStatusQ.addr = reinterpret_cast<uint8_t*>(&data_status);
    trx.ubrTx.capacity = 4;
    trx.ubrTx.writePos = 0;

    brpc::ubring::UBRing ring;
    ring._trx = &trx;

    const size_t too_large_payload = UBR_MSG_PAYLOAD_LEN * trx.ubrTx.capacity;
    EXPECT_EQ(UBRING_ERR, ring.WritevHasEnoughSpace(too_large_payload));
}
