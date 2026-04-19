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

#ifndef BRPC_SHM_DEF_H
#define BRPC_SHM_DEF_H
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>

#define PROT_READ 0x1 /* Page can be read.  */
#define PROT_WRITE 0x2 /* Page can be written.  */
#define PROT_EXEC 0x4 /* Page can be executed.  */
#define PROT_NONE 0x0 /* Page can not be accessed.  */
#define PROT_GROWSDOWN 0x01000000 /* Extend change to start of growsdown vma (mprotect only).  */
#define PROT_GROWSUP 0x02000000 /* Extend change to start of growsup vma (mprotect only).  */
/* Sharing types (must choose one and only one of these).  */
#define MAP_SHARED 0x01 /* Share changes.  */
#define MAP_PRIVATE 0x02 /* Changes are private.  */
#define SHM_MAX_NAME_BUFF_LEN 48 // byte, buffer size, ubsm_sdk need name to be below 48byte
#define SHM_MAX_NAME_LEN (SHM_MAX_NAME_BUFF_LEN - 1) // byte, string length
#define SHM_ALLOC_UNIT_SIZE (4 * 1024 * 1024) // 4MB

namespace brpc {
    namespace ubring {
        typedef enum { SHM_TYPE_UB, SHM_TYPE_IPC, SHM_TYPE_UBS, SHM_TYPE_UNSUPPORT } SHM_TYPE;

        typedef struct {
            uint8_t *addr;
            size_t len;
            uint64_t memid;
            char name[SHM_MAX_NAME_BUFF_LEN];
            uint32_t fd;
        } SHM;

        typedef struct ShmListNode {
            SHM shm;
            struct ShmListNode *next;
            struct ShmListNode *prev;
        } ShmListNode;

        typedef struct {
            ShmListNode* head;
            ShmListNode* tail;
            size_t size;
            pthread_mutex_t shmLock;
        } ShmList;
    }
}
#endif //BRPC_SHM_DEF_H