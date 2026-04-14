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

#ifndef BRPC_SHM_IPC_H
#define BRPC_SHM_IPC_H


#include "shm_def.h"

#define SHM_IPC_MODE 0666

namespace brpc {
    namespace ub {
        RETURN_CODE IpcShmLocalMalloc(SHM *shm);
        RETURN_CODE IpcShmMunmap(SHM *shm);
        RETURN_CODE IpcShmFree(SHM *shm);
        RETURN_CODE IpcShmLocalFree(SHM *shm);
        RETURN_CODE IpcShmRemoteMalloc(SHM *shm);
        RETURN_CODE IpcShmRemoteFree(SHM *shm);
        RETURN_CODE IpcShmLocalMmap(SHM *shm, int prot);
    }
}

#endif //BRPC_SHM_IPC_H