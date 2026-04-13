#ifndef BRPC_SHM_MGR_H
#define BRPC_SHM_MGR_H

#include <stdint.h>
#include "brpc/ub/common/common.h"
#include "brpc/ub/shm/shm_def.h"

namespace brpc {
namespace ub {
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