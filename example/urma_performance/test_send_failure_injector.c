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

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "urma_api.h"

#ifdef URMA_INJECTOR_TEST_PROVIDER

static int g_posts;
int UrmaInjectorTestPostCount(void) { return g_posts; }

urma_status_t urma_post_jetty_send_wr(urma_jetty_t* jetty, urma_jfs_wr_t* wr,
                                      urma_jfs_wr_t** bad_wr) {
    (void)jetty;
    (void)wr;
    if (bad_wr) {
        *bad_wr = NULL;
    }
    ++g_posts;
    return URMA_SUCCESS;
}

#else

int UrmaInjectorTestPostCount(void);
#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr, "Check failed: %s at line %d\n", #expr, __LINE__); \
            return 1;                                                          \
        }                                                                      \
    } while (0)

int main(void) {
    char marker[] = "/tmp/brpc-urma-injector-XXXXXX";
    int fd = mkstemp(marker);
    CHECK(fd >= 0);
    close(fd);
    CHECK(unlink(marker) == 0);
    CHECK(setenv("BRPC_URMA_FAIL_ONCE_FILE", marker, 1) == 0);
    urma_jfs_wr_t wr = {0};
    urma_jfs_wr_t* bad = NULL;
    wr.opcode = URMA_OPC_SEND;
    CHECK(urma_post_jetty_send_wr(NULL, &wr, &bad) == URMA_SUCCESS);
    CHECK(UrmaInjectorTestPostCount() == 1);

    FILE* file = fopen(marker, "w");
    CHECK(file != NULL);
    fclose(file);
    wr.opcode = URMA_OPC_SEND_IMM;
    CHECK(urma_post_jetty_send_wr(NULL, &wr, &bad) == URMA_SUCCESS);
    CHECK(UrmaInjectorTestPostCount() == 2);
    CHECK(access(marker, F_OK) == 0);

    wr.opcode = URMA_OPC_SEND;
    errno = 0;
    CHECK(urma_post_jetty_send_wr(NULL, &wr, &bad) == EIO);
    CHECK(errno == EIO && bad == &wr && UrmaInjectorTestPostCount() == 2);
    CHECK(access(marker, F_OK) != 0);
    CHECK(urma_post_jetty_send_wr(NULL, &wr, &bad) == URMA_SUCCESS);
    CHECK(UrmaInjectorTestPostCount() == 3);

    file = fopen(marker, "w");
    CHECK(file != NULL);
    fclose(file);
    CHECK(urma_post_jetty_send_wr(NULL, &wr, &bad) == URMA_SUCCESS);
    CHECK(UrmaInjectorTestPostCount() == 4);
    CHECK(unlink(marker) == 0);
    puts(
        "PASS: forwarding, credit ACK, single failure, and resumed forwarding");
    return 0;
}

#endif
