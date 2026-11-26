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

#ifndef BUTIL_MODP_B64_AVX2_H
#define BUTIL_MODP_B64_AVX2_H

#include <stddef.h>

#if (defined(__i386__) || defined(__x86_64__)) && \
    (defined(__GNUC__) || defined(__clang__))
#define MODP_B64_HAS_AVX2 1

#ifdef __cplusplus
extern "C" {
#endif

int modp_b64_avx2_supported(void);
size_t modp_b64_avx2_encode(char* dest, const char* src, size_t len);
size_t modp_b64_avx2_decode(char* dest, const char* src, size_t len);

#ifdef __cplusplus
}
#endif

#else
#define MODP_B64_HAS_AVX2 0
#endif

#endif  // BUTIL_MODP_B64_AVX2_H
