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

#define LOCK_GUARD(mtx_ptr)                                              \
    pthread_mutex_t *__attribute__((cleanup(UnlockMutex))) _mtx_ptr = ({ \
        pthread_mutex_lock(&(mtx_ptr));                                  \
        &(mtx_ptr);                                                      \
    })

static inline void UnlockSpinLock(pthread_spinlock_t **spin_lock)
{
    if (LIKELY(spin_lock != NULL && *spin_lock != NULL)) {
        pthread_spin_unlock(*spin_lock);
    } else {
        LOG(ERROR) << "Invalid input for spin_lock.";
    }
}

#define SPIN_LOCK_GUARD(spin_lock_ptr)                                               \
    pthread_spinlock_t *__attribute__((cleanup(UnlockSpinLock))) _spin_lock_ptr = ({ \
        pthread_spin_lock(&(spin_lock_ptr));                                         \
        &(spin_lock_ptr);                                                            \
    })

static inline void UnlockRWLock(pthread_rwlock_t **rw_lock)
{
    if (LIKELY(rw_lock != NULL && *rw_lock != NULL)) {
        pthread_rwlock_unlock(*rw_lock);
    } else {
        LOG(ERROR) << "Invalid input for rw_lock.";
    }
}

#define R_LOCK_GUARD(read_lock_ptr)                                               \
    pthread_rwlock_t *__attribute__((cleanup(UnlockRWLock))) _read_lock_ptr = ({ \
        pthread_rwlock_rdlock(&(read_lock_ptr));                                         \
        &(read_lock_ptr);                                                            \
    })

#define W_LOCK_GUARD(write_lock_ptr)                                               \
    pthread_rwlock_t *__attribute__((cleanup(UnlockRWLock))) _write_lock_ptr = ({ \
        pthread_rwlock_wrlock(&(write_lock_ptr));                                         \
        &(write_lock_ptr);                                                            \
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

#define SEMAPHORE_WAIT_GUARD_WITH_CLOSE(sem_ptr)                        \
    sem_t *__attribute__((cleanup(PostSemWithClose))) _sem_ptr = ({    \
        sem_wait(sem_ptr);                                               \
        sem_ptr;                                                         \
    })

#define SEMAPHORE_WAIT_GUARD(sem_ptr)                                   \
    sem_t *__attribute__((cleanup(PostSem))) _sem_ptr = ({    \
        sem_wait(sem_ptr);                                               \
        sem_ptr;                                                         \
    })

#ifdef __cplusplus
}
#endif
#endif //BRPC_THREAD_LOCK_H