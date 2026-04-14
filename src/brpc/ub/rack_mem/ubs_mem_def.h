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

#ifndef BRPC_UBS_MEM_DEF_H
#define BRPC_UBS_MEM_DEF_H
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SHMEM_API
#define SHMEM_API __attribute__((visibility("default")))
#endif

// 先修改为48，与旧版本对齐
#define MAX_HOST_NUM 16
#define MAX_NUMA_NUM 32
#define MAX_NUMA_RESV_LEN 16

#define MAX_HOST_NAME_DESC_LENGTH 64
#define MAX_SHM_NAME_LENGTH 48
#define MAX_REGION_NAME_DESC_LENGTH 48
#define MAX_REGION_NODE_NUM 16
#define MAX_REGIONS_NUM 6
#define MAX_OBMM_SHMDEV_PATH_LEN 64

#define MAX_MEMID_NUM 2048
#define MAX_SHM_CNT 300

#define UBSM_FLAG_CACHE 0x0UL
#define UBSM_FLAG_WITH_LOCK 0x1UL
#define UBSM_FLAG_NONCACHE 0x2UL  // open O_SYNC
#define UBSM_FLAG_WR_DELAY_COMP 0x4UL // obmm import with wr_delay_comp
#define UBSM_FLAG_ONLY_IMPORT_NONCACHE 0x8UL // only import open O_SYNC
#define UBSM_FLAG_MEM_ANONYMOUS 0x10UL // auto cleanup when all references in domain drop to zero

typedef enum {
    UBSM_OK = 0,
    // common error
    UBSM_ERR_PARAM_INVALID = 6010,
    UBSM_ERR_NOPERM = 6011,  // no permision
    UBSM_ERR_MEMORY = 6012,  // memcpy or other mem func failed
    UBSM_ERR_UNIMPL = 6013,  // not implement
    UBSM_CHECK_RESOURCE_ERROR = 6014, // resource check failed.
    UBSM_ERR_MEMLIB = 6015,  // mem lib failed
    UBSM_ERR_NO_NEEDED = 6016, // default region no need to create

    // resource error
    UBSM_ERR_NOT_FOUND = 6020,
    UBSM_ERR_ALREADY_EXIST = 6021,
    UBSM_ERR_MALLOC_FAIL = 6022,
    UBSM_ERR_RECORD = 6023,
    UBSM_ERR_IN_USING = 6024, // shm is in use (usrNum > 0)

    // net error
    UBSM_ERR_NET = 6040,

    // under api
    UBSM_ERR_UBSE = 6050,
    UBSM_ERR_OBMM = 6051,

    // cc lock error
    UBSM_ERR_LOCK_NOT_SUPPORTED = 6060,
    UBSM_ERR_LOCK_ALREADY_LOCKED = 6061,
    UBSM_ERR_DLOCK = 6062,

    UBSM_ERR_BUFF = 6099,
} ubsmshmem_ret_t;
/**
 * Memory distance, describes the physical memory resource distance relative to the current PE.
 */
typedef enum {
    /** direct connect node is provided, same as PerfLevel::L0 */
    DISTANCE_DIRECT_NODE = 0,
    /** one hop connect node is provided, same as PerfLevel::L1, not support 930 */
    DISTANCE_HOP_NODE = 1,
} ubsmem_distance_t;

typedef struct {
    // todo
} ubsmem_options_t;

typedef struct {
    char host_name[MAX_HOST_NAME_DESC_LENGTH];  // include '\0'
    bool affinity;
} ubsmem_region_node_desc_t;

typedef struct {
    int host_num;
    ubsmem_region_node_desc_t hosts[MAX_REGION_NODE_NUM];
} ubsmem_region_attributes_t;

typedef struct {
    int num;
    ubsmem_region_attributes_t region[MAX_REGIONS_NUM];
} ubsmem_regions_t;

typedef struct {
    char region_name[MAX_REGION_NAME_DESC_LENGTH];
    size_t size;
    ubsmem_region_attributes_t region_attr;
} ubsmem_region_desc_t;

typedef struct {
    uint32_t slot_id;            // 节点唯一标识, 采用slotid, 与lcne保持一致
    uint32_t socket_id;          // socket id
    uint32_t numa_id;            // 节点中的numa id
    uint32_t mem_lend_ratio;     // 池化内存借出比例上限
    uint64_t mem_total;          // 内存总量, 单位字节
    uint64_t mem_free;           // 内存空闲量, 单位字节
    uint64_t mem_borrow;         // 借用的内存，单位字节
    uint64_t mem_lend;           // 借出的内存，单位字节
    uint8_t resv[MAX_NUMA_RESV_LEN];
} ubsmem_numa_mem_t;

typedef struct {
    char host_name[MAX_HOST_NAME_DESC_LENGTH];
    int numa_num;
    ubsmem_numa_mem_t numa[MAX_NUMA_NUM];
} ubsmem_host_info_t;

typedef struct {
    int host_num;  // 集群可用节点数量
    ubsmem_host_info_t host[MAX_HOST_NUM];
} ubsmem_cluster_info_t;

typedef struct {
    char name[MAX_SHM_NAME_LENGTH + 1];
    size_t size;
} ubsmem_shmem_desc_t;

typedef struct {
    char name[MAX_SHM_NAME_LENGTH + 1];
    size_t size;
    uint32_t mem_num;
    uint64_t mem_unit_size;
    uint64_t mem_id_list[MAX_MEMID_NUM];
} ubsmem_shmem_info_t;

typedef int32_t (*shmem_faults_func)(const char *shm_name);

#ifdef __cplusplus
}
#endif
#endif //BRPC_UBS_MEM_DEF_H