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

#ifndef BRPC_UBS_MEM_H
#define BRPC_UBS_MEM_H
#include "ubs_mem_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the UBSMSHMEM attributes
 *
 * @param ubsm_shmem_opts - [out] shmem attributes
 * @return - 0 on success and other on failure
 */
SHMEM_API int ubsmem_init_attributes(ubsmem_options_t *ubsm_shmem_opts);

/**
 * Initialize the UBSMSHMEM library.
 * Required to be the first called when a process uses the UBSMSHMEM library.
 * @param ubsm_shmem_opts - options structure containing initialization choices
 * @return - 0 on success and other on failure
 */
SHMEM_API int ubsmem_initialize(const ubsmem_options_t *ubsm_shmem_opts);

/**
 * Finalize the UBSMSHMEM library.
 * Once finalized, the process can continue work,but it is disconnected from the UBSMSHMEM library functions.
 * @return - 0 on success and other on failure
 */
SHMEM_API int ubsmem_finalize(void);

/**
 * @brief Set log level
 * @return - 0 on success and other on failure
 * @param level - level to be set, debug(0), info(1), warning(2), error(3), closed(4)
 */
SHMEM_API int ubsmem_set_logger_level(int level);

/**
 * @brief Set external log function, user can set customized logger function,
 * in the customized logger function, user can use unified logger utility,
 * then the log message can be written into the same log file as caller's,
 * if it is not set, log message will be printed to stdout.
 * @param func - [in] external logger function
 * @return 0 on success and other on failure
 */
SHMEM_API int ubsmem_set_extern_logger(void (*func)(int level, const char *msg));

/**
 * Look up regions in UBSMSHMEM associated with the local node.
 * @param regions - [out] The descriptor to the regions.
 * @return - 0 on success and other on failure
 */
SHMEM_API int ubsmem_lookup_regions(ubsmem_regions_t* regions);

/**
 * Create a large region of UBSMSHMEM.
 * Regions are primarily used as large containers within which additional memory may be allocated and managed by
 * the program.
 * @param region_name - name of the region
 * @param size - size (in bytes) requested for the region, 930 no use, default 0.
 * Note that implementations may round up the size to implementation-dependent sizes,
 * and may impose system-wide (or user-dependent) limits on individual and total size allocated to a given user.
 * @param reg_attr - details of UBSMSHMEM region attributes
 * @param region_desc - [out] Region_Descriptor for the created region
 * @return - 0 on success and other on failure
 */
SHMEM_API int ubsmem_create_region(const char *region_name, size_t size, const ubsmem_region_attributes_t *reg_attr);

/**
 * Look up a region in UBSMSHMEM by name in the name service.
 * @param region_name - name of the region.
 * @param region_desc - [out] The descriptor to the region.
 * @return - 0 on success and other on failure
 */
SHMEM_API int ubsmem_lookup_region(const char *region_name, ubsmem_region_desc_t *region_desc);

/**
 * Destroy a region, and all contents within the region. Note that this
 * method call will trigger a delayed free operation to permit other
 * instances currently using the region to finish.
 * @param region_name - name of the region.
 * @return - 0 on success and other on failure
 */
SHMEM_API int ubsmem_destroy_region(const char *region_name);

/**
 * Allocate some named space within a region. Allocates an area of UBSMSHMEM within a region
 * @param region_name - name of the region.
 * @param name - name of the share memory object
 * @param size - size of the space to allocate in bytes.
 * @param mode - mode associated with this space.
 * @param flags - Special marking for this object, MXMEM_FLAG_WITH_LOCK etc.
 * @return - 0 on success and other on failure
 */
SHMEM_API int ubsmem_shmem_allocate(const char *region_name, const char *name, size_t size, mode_t mode,
    uint64_t flags);

/**
 * Deallocate allocated space in memory
 * @param name - name of the share memory object
 * @return - 0 on success and other on failure
 */
SHMEM_API int ubsmem_shmem_deallocate(const char *name);

/**
 * Map item in UBSMSHMEM to the local virtual address space, and return its pointer.
 * @param addr - The starting address for the new mapping is specified in addr, If addr is NULL, then
 * the kernel chooses the (page-aligned) address at which to create the mapping
 * @param length - The length argument specifies the length of the mapping (which must be greater than 0)
 * @param prot - same as mmap, describes the desired memory protection of the mapping (and must not conflict with
 * the open mode of the file).
 * @param flags - same as mmap
 * @param name - name of the share memory object which to be mapped, same as mmap's fd
 * @param offset - same as mmap, offset must be a multiple of the page size
 * @param local_ptr - [out] within the process virtual address space that can be used to directly access the
 * data item in UBSMSHMEM
 * @return - 0 on success and other on failure
 */
SHMEM_API int ubsmem_shmem_map(void *addr, size_t length, int prot, int flags, const char *name, off_t offset,
                               void **local_ptr);

/**
 * Unmap a data item in UBSMSHMEM from the local virtual address space.
 * @param local_ptr - pointer within the process virtual address space to be unmapped
 * @param length - the size to be unmapped
 * @return - 0 on success and other on failure
 */
SHMEM_API int ubsmem_shmem_unmap(void *local_ptr, size_t length);

/**
 * Change permissions associated with a data item descriptor.
 * @param name - descriptor associated with some data item
 * @param perm - new permissions for the data item
 * @return - 0 on success and other on failure,other return described in UBSM_SHMEM_RETURN.
 */
SHMEM_API int ubsmem_shmem_set_ownership(const char *name, void *start, size_t length, int prot);

/**
 * shmem lock - Set the lock, status, and data consistency of the shmem item
 * @param name - descriptor associated with share memory object
 * @return - 0 on success and other on failure
 */
SHMEM_API int ubsmem_shmem_write_lock(const char *name);
SHMEM_API int ubsmem_shmem_read_lock(const char *name);
SHMEM_API int ubsmem_shmem_unlock(const char *name);

SHMEM_API int ubsmem_shmem_list_lookup(const char *prefix, ubsmem_shmem_desc_t *shm_list, uint32_t *shm_cnt);
SHMEM_API int ubsmem_shmem_lookup(const char *name, ubsmem_shmem_info_t *shm_info);
SHMEM_API int ubsmem_shmem_attach(const char *name);
SHMEM_API int ubsmem_shmem_detach(const char *name);

/**
 * Alloc an area from the resource pool and use it only within the scope of the current process.
 * @param region_name - name of the region.
 * @param size - size of the space to allocate in bytes.
 * Note that implementations may round up the size to implementation-dependent sizes.
 * @param mem_distance - Describe the performance distance between memory resources and local nodes.
 * Note that described in perf_desc_distance
 * @param is_numa - is numa or fd malloc, true: numa, false: fd
 * @param local_ptr - [out] pointer within the process virtual address space that can be used to directly access.
 * @return - 0 on success and other on failure
 */
SHMEM_API int ubsmem_lease_malloc(const char *region_name, size_t size, ubsmem_distance_t mem_distance, bool is_numa,
                                  void **local_ptr);

/**
 * Release the pointer.
 * @param local_ptr - The pointer returned by the malloc function.
 * @return - 0 on success and other on failure
 */
SHMEM_API int ubsmem_lease_free(void *local_ptr);

SHMEM_API int ubsmem_lookup_cluster_statistic(ubsmem_cluster_info_t *info);

/**
 * Subscribes to shared memory UB Event.
 * @param register_func - Shared Memory UB Event Response Handling Function.
 * @return - 0 on success and other on failure
 */
SHMEM_API int ubsmem_shmem_faults_register(shmem_faults_func register_func);

/**
 * Query the supernode ID of this node within the supernode domain.
 * @param nid - The supernode ID of this node within the supernode domain.
 * @return - 0 on success and other on failure
 */
SHMEM_API int ubsmem_local_nid_query(uint32_t *nid);

#ifdef __cplusplus
}  // end of extern "C"
#endif
#endif //BRPC_UBS_MEM_H