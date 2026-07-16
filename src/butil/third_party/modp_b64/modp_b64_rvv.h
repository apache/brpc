/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 4 -*- */
/* vi: set expandtab shiftwidth=4 tabstop=4: */
/**
 * \file
 * RISC-V Vector (RVV) accelerated base64 encoder/decoder declarations
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * License); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * AS IS BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef BUTIL_MODP_B64_RVV_H
#define BUTIL_MODP_B64_RVV_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__riscv) && defined(__riscv_vector)
#include <riscv_vector.h>

size_t rvv_modp_b64_encode(char* dest, const char* str, size_t len);
size_t rvv_modp_b64_decode(char* dest, const char* src, size_t len);
#endif /* __riscv_vector */

#ifdef __cplusplus
}
#endif

#endif /* BUTIL_MODP_B64_RVV_H */
