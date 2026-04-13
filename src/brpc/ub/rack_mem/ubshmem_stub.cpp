#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>
#include "ubs_mem.h"

int ubsmem_init_attributes(ubsmem_options_t *ubsm_shmem_opts)
{
    return UBSM_OK;
}

int ubsmem_initialize(const ubsmem_options_t *ubsm_shmem_opts)
{
    return UBSM_OK;
}

int ubsmem_finalize(void)
{
    return UBSM_OK;
}

int ubsmem_set_logger_level(int level)
{
    return UBSM_OK;
}

int ubsmem_set_extern_logger(void (*func)(int level, const char *msg))
{
    return UBSM_OK;
}

int ubsmem_lookup_regions(ubsmem_regions_t* regions)
{
    regions->num = 1;
    regions->region[0].host_num = 1;
    regions->region[0].hosts[0].affinity = true;
    regions->region[0].hosts[0].host_name[0] = 'h';
    regions->region[0].hosts[0].host_name[1] = '1';
    regions->region[0].hosts[0].host_name[2] = '\0'; // 2号位置使用\0
    return UBSM_OK;
}

int ubsmem_create_region(const char *region_name, size_t size, const ubsmem_region_attributes_t *reg_attr)
{
    return UBSM_OK;
}


int ubsmem_destroy_region(const char *region_name)
{
    return UBSM_OK;
}

int ubsmem_shmem_allocate(const char *region_name, const char *name, size_t size, mode_t mode, uint64_t flags)
{
    return UBSM_OK;
}

int ubsmem_shmem_deallocate(const char *name)
{
    return UBSM_OK;
}

int ubsmem_shmem_map(void *addr, size_t length, int prot, int flags, const char *name, off_t offset,
    void **local_ptr)
{
    return UBSM_OK;
}

int ubsmem_shmem_unmap(void *local_ptr, size_t length)
{
    return UBSM_OK;
}

int ubsmem_shmem_faults_register(shmem_faults_func registerFunc)
{
    return UBSM_OK;
}

int ubsmem_local_nid_query(uint32_t *nid)
{
    *nid = 1; // stub
    return UBSM_OK;
}