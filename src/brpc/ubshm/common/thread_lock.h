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
#include "brpc/ubshm/common/common.h"

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

#define LOCK_GUARD(mtx_ptr)                                             \
    pthread_mutex_t *__attribute__((cleanup(UnlockMutex))) _mtx_ptr = ({ \
        pthread_mutex_lock(&(mtx_ptr));                                 \
        &(mtx_ptr);                                                     \
    })

#ifdef __cplusplus
}
#endif
#endif //BRPC_THREAD_LOCK_H
