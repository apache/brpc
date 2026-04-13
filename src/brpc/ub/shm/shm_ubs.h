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