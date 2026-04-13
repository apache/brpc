//
// Created by z00926396 on 2026/4/11.
//

#ifndef BRPC_THREAD_LOCK_H
#define BRPC_THREAD_LOCK_H
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <semaphore.h>
#include <pthread.h>
#include "brpc/ub/common/common.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void UnlockMutex(pthread_mutex_t **mtx)
{
    if (LIKELY(mtx != NULL && *mtx != NULL)) {
        pthread_mutex_unlock(*mtx);
    } else {
        LOG(ERROR) << "Invalid input for mtx.";
    }
}

#define LOCK_GUARD(mtxPtr)                                              \
    pthread_mutex_t *__attribute__((cleanup(UnlockMutex))) _mtxPtr = ({ \
        pthread_mutex_lock(&(mtxPtr));                                  \
        &(mtxPtr);                                                      \
    })

static inline void UnlockSpinLock(pthread_spinlock_t **spinLock)
{
    if (LIKELY(spinLock != NULL && *spinLock != NULL)) {
        pthread_spin_unlock(*spinLock);
    } else {
        LOG(ERROR) << "Invalid input for spinLock.";
    }
}

#define SPIN_LOCK_GUARD(spinLockPtr)                                               \
    pthread_spinlock_t *__attribute__((cleanup(UnlockSpinLock))) _spinLockPtr = ({ \
        pthread_spin_lock(&(spinLockPtr));                                         \
        &(spinLockPtr);                                                            \
    })

static inline void UnlockRWLock(pthread_rwlock_t **rwLock)
{
    if (LIKELY(rwLock != NULL && *rwLock != NULL)) {
        pthread_rwlock_unlock(*rwLock);
    } else {
        LOG(ERROR) << "Invalid input for rwLock.";
    }
}

#define R_LOCK_GUARD(readLockPtr)                                               \
    pthread_rwlock_t *__attribute__((cleanup(UnlockRWLock))) _readLockPtr = ({ \
        pthread_rwlock_rdlock(&(readLockPtr));                                         \
        &(readLockPtr);                                                            \
    })

#define W_LOCK_GUARD(writeLockPtr)                                               \
    pthread_rwlock_t *__attribute__((cleanup(UnlockRWLock))) _writeLockPtr = ({ \
        pthread_rwlock_wrlock(&(writeLockPtr));                                         \
        &(writeLockPtr);                                                            \
    })

static inline void PostSemWithClose(sem_t **sem)
{
    if (LIKELY(sem != NULL && *sem != NULL)) {
        sem_post(*sem);
        sem_close(*sem);
        *sem = NULL;
        sem = NULL;
    } else {
        LOG(ERROR) << "Invalid input for semaphore.";
    }
}

static inline void PostSem(sem_t **sem)
{
    if (LIKELY(sem != NULL && *sem != NULL)) {
        sem_post(*sem);
    } else {
        LOG(ERROR) << "Invalid input for semaphore.";
    }
}

#define SEMAPHORE_WAIT_GUARD_WITH_CLOSE(semPtr)                        \
    sem_t *__attribute__((cleanup(PostSemWithClose))) _semPtr = ({    \
        sem_wait(semPtr);                                               \
        semPtr;                                                         \
    })

#define SEMAPHORE_WAIT_GUARD(semPtr)                                   \
    sem_t *__attribute__((cleanup(PostSem))) _semPtr = ({    \
        sem_wait(semPtr);                                               \
        semPtr;                                                         \
    })

#ifdef __cplusplus
}
#endif
#endif //BRPC_THREAD_LOCK_H