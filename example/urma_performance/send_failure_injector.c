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

#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "urma_api.h"

typedef urma_status_t (*PostSend)(urma_jetty_t*, urma_jfs_wr_t*,
                                  urma_jfs_wr_t**);
static PostSend g_post_send;
static pthread_once_t g_resolve_once = PTHREAD_ONCE_INIT;
static atomic_bool g_injected = ATOMIC_VAR_INIT(0);

static void ResolvePostSend(void) {
    g_post_send = (PostSend)dlsym(RTLD_NEXT, "urma_post_jetty_send_wr");
    if (!g_post_send) {
        fprintf(stderr, "URMA injector: cannot resolve real post_send: %s\n",
                dlerror());
    }
}

// LD_PRELOAD this module into the recovery client. Creating the marker named
// by BRPC_URMA_FAIL_ONCE_FILE rejects one data SEND before it reaches liburma.
// Credit ACKs and all subsequent sends are forwarded to the provider.
urma_status_t urma_post_jetty_send_wr(urma_jetty_t* jetty, urma_jfs_wr_t* wr,
                                      urma_jfs_wr_t** bad_wr) {
    pthread_once(&g_resolve_once, ResolvePostSend);
    if (!g_post_send) {
        if (bad_wr) {
            *bad_wr = wr;
        }
        errno = ENOSYS;
        return ENOSYS;
    }
    const char* marker = getenv("BRPC_URMA_FAIL_ONCE_FILE");
    if (wr && wr->opcode == URMA_OPC_SEND && marker && marker[0] &&
        access(marker, F_OK) == 0 && !atomic_exchange(&g_injected, 1)) {
        if (unlink(marker) != 0) {
            fprintf(stderr, "URMA injector: cannot remove marker: %s\n",
                    marker);
        }
        if (bad_wr) {
            *bad_wr = wr;
        }
        fprintf(
            stderr,
            "URMA injector: injected one data SEND post failure, errno=%d\n",
            EIO);
        errno = EIO;
        return EIO;
    }
    return g_post_send(jetty, wr, bad_wr);
}
