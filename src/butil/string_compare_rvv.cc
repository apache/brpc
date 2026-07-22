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

// RVV-accelerated memcmp and memchr for StringPiece operations.
// memcmp: follows glibc's RVV memcmp pattern (e8m8 LMUL, vfirst.m early-out).
// memchr: uses vmseq + vfirst.m to locate first matching byte.

#include "butil/strings/string_piece.h"

#if defined(__riscv) && defined(__riscv_vector)
#include <riscv_vector.h>

namespace butil {

int rvv_memcmp(const void* p1, const void* p2, size_t n) {
    const uint8_t* src1 = static_cast<const uint8_t*>(p1);
    const uint8_t* src2 = static_cast<const uint8_t*>(p2);
    size_t remaining = n;

    while (remaining > 0) {
        size_t vl = __riscv_vsetvl_e8m8(remaining);
        vuint8m8_t v1 = __riscv_vle8_v_u8m8(src1, vl);
        vuint8m8_t v2 = __riscv_vle8_v_u8m8(src2, vl);
        vbool1_t neq = __riscv_vmsne_vv_u8m8_b1(v1, v2, vl);
        long first = __riscv_vfirst_m_b1(neq, vl);

        if (first >= 0) {
            return static_cast<int>(src1[first]) - static_cast<int>(src2[first]);
        }

        src1 += vl;
        src2 += vl;
        remaining -= vl;
    }
    return 0;
}

const void* rvv_memchr(const void* s, int c, size_t n) {
    const uint8_t* src = static_cast<const uint8_t*>(s);
    uint8_t ch = static_cast<uint8_t>(c);
    size_t remaining = n;

    while (remaining > 0) {
        size_t vl = __riscv_vsetvl_e8m8(remaining);
        vuint8m8_t v = __riscv_vle8_v_u8m8(src, vl);
        vbool1_t eq = __riscv_vmseq_vx_u8m8_b1(v, ch, vl);
        long first = __riscv_vfirst_m_b1(eq, vl);

        if (first >= 0) {
            return src + first;
        }

        src += vl;
        remaining -= vl;
    }
    return nullptr;
}

}  // namespace butil

#endif  // __riscv && __riscv_vector
