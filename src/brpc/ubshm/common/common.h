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

#ifndef BRPC_COMMON_H
#define BRPC_COMMON_H
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include "butil/compiler_specific.h"
#include "butil/logging.h"

#define LIKELY(x) BAIDU_LIKELY(x)
#define UNLIKELY(x) BAIDU_UNLIKELY(x)

#ifndef UNREFERENCE_PARAM
#define UNREFERENCE_PARAM(x) ((void)(x))
#endif

#ifdef UT
#define STATIC
#define INLINE
#define UBRING_STATISTICS_PATH ROOT_PATH "/ubring/run"
#else
#define STATIC static
#define INLINE inline
#define UBRING_STATISTICS_PATH "/opt/ubring/run"
#endif

#ifdef __cplusplus
#include <atomic>
using AtomicInt = std::atomic<int>;
using AtomicBool = std::atomic<bool>;
using AtomicUintFast64 = std::atomic<uint_fast64_t>;
using AtomicUintFast8 = std::atomic<uint_fast8_t>;
#define ATOMIC_INIT(var, value) var.store(value)
#define ATOMIC_STORE(var, value) var.store(value)
#define ATOMIC_LOAD(var) var.load()
#define ATOMIC_ADD(var, value) var.fetch_add(value)
#define ATOMIC_SUB(var, value) var.fetch_sub(value)
#define ATOMIC_COMPARE_EXCHANGE_STRONG(var, expected, desired) \
    var.compare_exchange_strong((expected), (desired))
#else
#include <stdatomic.h>
typedef atomic_int AtomicInt;
typedef atomic_bool AtomicBool;
typedef atomic_uint_fast64_t AtomicUintFast64;
typedef atomic_uint_fast8_t AtomicUintFast8;
#define ATOMIC_INIT(var, value) atomic_init(&(var), value)
#define ATOMIC_STORE(var, value) atomic_store(&(var), value)
#define ATOMIC_LOAD(var) atomic_load(&(var))
#define ATOMIC_ADD(var, value) atomic_fetch_add(&(var), value)
#define ATOMIC_SUB(var, value) atomic_fetch_sub(&(var), value)
#define ATOMIC_COMPARE_EXCHANGE_STRONG(var, expected, desired) \
    atomic_compare_exchange_strong(&(var), &(expected), (desired))
#endif

#define ISB() __asm__ __volatile__("isb" ::: "memory")
#define DSB() __asm__ __volatile__("dsb sy" ::: "memory")

#ifndef errno_t
typedef int errno_t;
#endif
#ifndef EOK
#define EOK 0
#endif

#define MAX_NODE_NUM 8
#define IPV4_FIRST_BYTE_OFFSET 24
#define COPY_ALIGNED_DATA_BYTES 64

#if defined(OS_MACOSX)
#define EPOLLIN 0x001
#define EPOLLOUT 0x004
#define EPOLLET 0x80000000
#endif

static inline int Copy64Byte(int8_t *dst, int8_t *src) {
#ifdef LS64
    asm volatile (
        "mov x12, %0\n"
        "mov x13, %1\n"
        "ldr x4, [x12]\n"
        "ldr x5, [x12, #8]\n"
        "ldr x6, [x12, #16]\n"
        "ldr x7, [x12, #24]\n"
        "ldr x8, [x12, #32]\n"
        "ldr x9, [x12, #40]\n"
        "ldr x10, [x12, #48]\n"
        "ldr x11, [x12, #56]\n"
        "ST64B x4, [x13]\n"
        :
        : "r" (src), "r" (dst)
        : "memory", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13"
    );
    return EOK;
#else
    memcpy(dst, src, COPY_ALIGNED_DATA_BYTES);
    return EOK;
#endif
}

#define SEC_TO_NSEC 1000000000
#define MSEC_TO_NSEC 1000000
#define USEC_TO_NSEC 1000
#define MSEC_TO_SEC 1000
#define MAX_IP_PORT_STR_LEN 23
#define DECIMAL_BASE 10

static inline uint64_t GetCurNanoSeconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t timestamp = (uint64_t)ts.tv_sec * SEC_TO_NSEC + (uint64_t)ts.tv_nsec;
    return timestamp;
}

#define FREE_PTR(ptr) \
    do {              \
        if ((ptr) != NULL) { \
            free(ptr);    \
            (ptr) = NULL; \
        }             \
    } while (0)

typedef enum {
    UBRING_OK = 0,
    UBRING_ERR = -1,
    UBRING_RETRY = -2,
    UBRING_REENTRY = -3,
    UBRING_ERR_TIMEOUT = -4,
    SHM_ERR = -100,
    SHM_ERR_INPUT_INVALID = -101,
    SHM_ERR_EXIST = -102,
    SHM_ERR_RESOURCE_ATTACHED = -103,
    SHM_ERR_NOT_FOUND = -104,
    SHM_ERR_UBSM_NET_ERR = -105,
    MPA_UDP_ERR = -200,
    MPA_UDP_NO_TRX = -201,
    MPA_UDP_STATUS_NOT_JOINED = -202,
    MPA_MUXER_NOT_READY = -203,
    MPA_PORT_FULL = -204,
    MPA_PORT_OUTRANGE = -205,
    MPA_PORT_TAKEN = -206,
    MPA_UDP_STATUS_NOT_CONNECTED = -207,
    MPA_UDP_STATUS_ALREADY_CONNECTED = -208,
    MPA_UDP_OLD_RDLIST = -209,
    MPA_UDP_RDLIST_FULL = -210,
    UBR_NOT_CONNECTED = -300,
    UBR_ERR_ADDR_IN_USE = -301,
} RETURN_CODE;

#define ALIGN_BYTES 0x40
#define CHECKED_ALIGN_BITS (ALIGN_BYTES - 1)

static inline size_t Aligned64Offset(uint8_t *addr) {
    return ((ALIGN_BYTES - (((size_t)(addr)) & CHECKED_ALIGN_BITS)) & CHECKED_ALIGN_BITS);
}

static inline RETURN_CODE HasTimedOut(const uint64_t start_time, const uint32_t timeout) {
    uint64_t end_time = start_time + (uint64_t)timeout * SEC_TO_NSEC;
    if (GetCurNanoSeconds() > end_time) {
        LOG(ERROR) << "task time out " << timeout << " seconds.";
        return UBRING_ERR;
    }
    return UBRING_OK;
}

#endif // BRPC_COMMON_H