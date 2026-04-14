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

#ifndef BRPC_SHM_UBS_H
#define BRPC_SHM_UBS_H
namespace brpc {
namespace ub {
DECLARE_int32(ub_flying_io_timeout);

typedef enum TagUbsLogLevel {
    UBSM_LOG_DEBUG_LEVEL = 0,
    UBSM_LOG_INFO_LEVEL = 1,
    UBSM_LOG_WARN_LEVEL = 2,
    UBSM_LOG_ERROR_LEVEL = 3,
    UBSM_LOG_CLOSED_LEVEL = 4
} UbsLogLevel;

RETURN_CODE UbsShmLocalMalloc(SHM *shm);
RETURN_CODE UbsShmMunmap(SHM *shm);
RETURN_CODE UbsShmFree(SHM *shm);
RETURN_CODE UbsShmLocalFree(SHM *shm);
RETURN_CODE UbsShmRemoteMalloc(SHM *shm);
RETURN_CODE UbsShmRemoteFree(SHM *shm);
RETURN_CODE UbsShmInit(void);
RETURN_CODE UbsShmFini(void);
RETURN_CODE UbsShmLocalMmap(SHM *shm, int prot);
void UbsMemLoggerPrint(int level, const char *msg);

void *UbsShmCallback(void* args);
RETURN_CODE UbsShmAddTimer(ShmList *shmList);
RETURN_CODE InitShmTimer(ShmList **shmList);
RETURN_CODE DestroyShmTimer(ShmList *shmList);
RETURN_CODE AddShmToList(ShmList *shmList, SHM *shm);
RETURN_CODE IsExistInShmList(ShmList *shmList, const SHM *shm);
}
}
#endif //BRPC_SHM_UBS_H