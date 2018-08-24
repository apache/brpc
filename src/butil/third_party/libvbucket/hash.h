/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2010 NorthScale, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */
#ifndef LIBVBUCKET_HASH_H
#define LIBVBUCKET_HASH_H 1

#include <stdint.h>
#include <sys/types.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
namespace butil {
#endif

uint32_t hash_crc32(const char *key, size_t key_length);
uint32_t hash_ketama(const char *key, size_t key_length);
void hash_md5(const char *key, size_t key_length, unsigned char *result);

void* hash_md5_update(void *ctx, const char *key, size_t key_length);
void hash_md5_final(void *ctx, unsigned char *result);

#ifdef __cplusplus
}  // namespace butil
}
#endif

#endif
