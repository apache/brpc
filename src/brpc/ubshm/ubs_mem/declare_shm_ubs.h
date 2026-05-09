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

#ifndef UBRING_MK_UBSM
#error Do not include this file unless you know what you are doing.
#endif

#ifndef UBRING_MK_UBSM_OPTIONAL
#define UBRING_MK_UBSM_OPTIONAL UBRING_MK_UBSM
#endif

UBRING_MK_UBSM(int, ubsmem_init_attributes, (ubsmem_options_t *ubsm_shmem_opts));

UBRING_MK_UBSM(int, ubsmem_initialize, (const ubsmem_options_t *ubsm_shmem_opts));

UBRING_MK_UBSM(int, ubsmem_finalize, (void));

UBRING_MK_UBSM(int, ubsmem_set_logger_level, (int level));

UBRING_MK_UBSM(int, ubsmem_set_extern_logger, (void (*func)(int level, const char *msg)));

UBRING_MK_UBSM(int, ubsmem_lookup_regions, (ubsmem_regions_t* regions));

UBRING_MK_UBSM(int, ubsmem_create_region, (const char *region_name, size_t size, const ubsmem_region_attributes_t *reg_attr));

UBRING_MK_UBSM(int, ubsmem_destroy_region, (const char *region_name));

UBRING_MK_UBSM(int, ubsmem_shmem_allocate,(const char *region_name, const char *name, size_t size, mode_t mode,
    uint64_t flags));

UBRING_MK_UBSM(int, ubsmem_shmem_deallocate, (const char *name));

UBRING_MK_UBSM(int, ubsmem_shmem_map, (void *addr, size_t length, int prot, int flags, const char *name, off_t offset,
    void **local_ptr));

UBRING_MK_UBSM(int, ubsmem_shmem_unmap, (void *local_ptr, size_t length));

UBRING_MK_UBSM(int, ubsmem_shmem_faults_register, (shmem_faults_func registerFunc));

UBRING_MK_UBSM(int, ubsmem_local_nid_query, (uint32_t *nid));

#undef UBRING_MK_UBSM_OPTIONAL
#undef UBRING_MK_UBSM