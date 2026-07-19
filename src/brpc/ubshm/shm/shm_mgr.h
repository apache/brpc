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

#ifndef BRPC_SHM_MGR_H
#define BRPC_SHM_MGR_H

#include <stdint.h>
#include "brpc/ubshm/common/common.h"
#include "brpc/ubshm/shm/shm_def.h"

namespace brpc {
namespace ubring {
void SetShmType(SHM_TYPE type);

RETURN_CODE ShmMgrInit(void);

void ShmMgrFini(void);

RETURN_CODE ShmLocalMalloc(SHM *shm);

RETURN_CODE ShmLocalCalloc(SHM *shm);

RETURN_CODE ShmLocalFree(SHM *shm);

RETURN_CODE ShmRemoteMalloc(SHM *shm);

RETURN_CODE ShmRemoteFree(SHM *shm);

RETURN_CODE ShmLocalMmap(SHM *shm, int prot);

RETURN_CODE ShmMunmap(SHM *shm);

RETURN_CODE ShmFree(SHM *shm);
}
}

#endif //BRPC_SHM_MGR_H