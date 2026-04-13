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