/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 4 -*- */
/* vi: set expandtab shiftwidth=4 tabstop=4: */
/**
 * \file
 * RISC-V Vector (RVV) accelerated base64 encoder/decoder implementation
 *
 * Optimizes table lookup using RVV vrgather (encode) and vectorized
 * range comparisons (decode) for significant speedup on rv64gcv hardware.
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

#include "modp_b64_rvv.h"

#if defined(__riscv) && defined(__riscv_vector)
#include <riscv_vector.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 64-entry base64 alphabet (A-Z, a-z, 0-9, +, /) */
static const uint8_t s_rvv_alphabet[64] = {
    'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
    'Q','R','S','T','U','V','W','X','Y','Z','a','b','c','d','e','f',
    'g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v',
    'w','x','y','z','0','1','2','3','4','5','6','7','8','9','+','/'
};

/**
 * rvv_modp_b64_encode - RVV-accelerated base64 encode
 *
 * Processes 12 input bytes at a time (producing 16 output bytes):
 *   1. Compute 16 6-bit indices from 4 input triplets via scalar bit manipulation
 *   2. Vectorized vrgather lookup from 64-entry alphabet (LMUL=4)
 *   3. Vectorized store of 16 bytes
 *
 * The alphabet (64 bytes) is preloaded into LMUL=4 register group.
 */
size_t rvv_modp_b64_encode(char* dest, const char* str, size_t len)
{
    size_t i = 0;
    uint8_t* p = (uint8_t*)dest;

    /* Preload 64-byte alphabet into LMUL=4 register group */
    size_t vl_alpha = __riscv_vsetvl_e8m4(64);
    vuint8m4_t v_alpha = __riscv_vle8_v_u8m4(s_rvv_alphabet, vl_alpha);

    /* Process 12 bytes (4 triplets -> 16 output) per iteration */
    while (i + 12 <= len) {
        uint8_t idx_buf[16] __attribute__((aligned(16)));

        /* Compute 16 6-bit indices from 4 input triplets (scalar) */
        for (int j = 0; j < 4; j++) {
            uint8_t b0 = (uint8_t)str[i + j*3];
            uint8_t b1 = (uint8_t)str[i + j*3 + 1];
            uint8_t b2 = (uint8_t)str[i + j*3 + 2];
            idx_buf[j*4]     = b0 >> 2;
            idx_buf[j*4 + 1] = ((b0 & 0x03) << 4) | (b1 >> 4);
            idx_buf[j*4 + 2] = ((b1 & 0x0F) << 2) | (b2 >> 6);
            idx_buf[j*4 + 3] = b2 & 0x3F;
        }

        /* Vectorized lookup: alphabet[idx[i]] via vrgather */
        size_t vl = __riscv_vsetvl_e8m4(16);
        vuint8m4_t v_idx = __riscv_vle8_v_u8m4(idx_buf, vl);
        vuint8m4_t v_out = __riscv_vrgather_vv_u8m4(v_alpha, v_idx, vl);
        __riscv_vse8_v_u8m4(p, v_out, vl);

        p += 16;
        i += 12;
    }

    /* Tail processing: remaining bytes (0-11) handled via scalar fallback */
    {
        size_t remaining = len - i;
        const uint8_t* s = (const uint8_t*)(str + i);
        uint8_t* d = p;

        uint8_t t1, t2, t3;
        size_t ti = 0;

        if (remaining > 2) {
            for (; ti < remaining - 2; ti += 3) {
                t1 = s[ti]; t2 = s[ti+1]; t3 = s[ti+2];
                *d++ = s_rvv_alphabet[t1 >> 2];
                *d++ = s_rvv_alphabet[((t1 & 0x03) << 4) | ((t2 >> 4) & 0x0F)];
                *d++ = s_rvv_alphabet[((t2 & 0x0F) << 2) | ((t3 >> 6) & 0x03)];
                *d++ = s_rvv_alphabet[t3 & 0x3F];
            }
        }

        switch (remaining - ti) {
        case 1:
            t1 = s[ti];
            *d++ = s_rvv_alphabet[t1 >> 2];
            *d++ = s_rvv_alphabet[(t1 & 0x03) << 4];
            *d++ = '=';
            *d++ = '=';
            break;
        case 2:
            t1 = s[ti]; t2 = s[ti+1];
            *d++ = s_rvv_alphabet[t1 >> 2];
            *d++ = s_rvv_alphabet[((t1 & 0x03) << 4) | ((t2 >> 4) & 0x0F)];
            *d++ = s_rvv_alphabet[(t2 & 0x0F) << 2];
            *d++ = '=';
            break;
        default:
            break;
        }

        return (size_t)(d - (uint8_t*)dest);
    }
}

/**
 * rvv_modp_b64_decode - RVV-accelerated base64 decode
 *
 * Processes 16 input bytes at a time (producing 12 output bytes):
 *   1. Load 16 base64 chars into a vector
 *   2. Classify each char into one of 5 ranges (A-Z, a-z, 0-9, +, /)
 *      using vectorized comparisons
 *   3. Compute decoded 6-bit values for each range
 *   4. Merge via vmerge chain
 *   5. Pack 4x6-bit -> 3 bytes in scalar
 *
 * Returns number of decoded bytes, or MODP_B64_ERROR on invalid input.
 */
#define MODP_B64_RVV_ERROR ((size_t)-1)

size_t rvv_modp_b64_decode(char* dest, const char* src, size_t len)
{
    if (len == 0) return 0;

    size_t i = 0;
    uint8_t* p = (uint8_t*)dest;

    /* Process 16 input bytes (12 output) per iteration */
    while (i + 16 <= len) {
        size_t vl = __riscv_vsetvl_e8m1(16);
        vuint8m1_t v = __riscv_vle8_v_u8m1((const uint8_t*)(src + i), vl);

        /* Range classification masks */
        vbool8_t m_upper = __riscv_vmsgeu_vx_u8m1_b8(v, 'A', vl);
        m_upper = __riscv_vmand_mm_b8(m_upper, __riscv_vmsleu_vx_u8m1_b8(v, 'Z', vl), vl);

        vbool8_t m_lower = __riscv_vmsgeu_vx_u8m1_b8(v, 'a', vl);
        m_lower = __riscv_vmand_mm_b8(m_lower, __riscv_vmsleu_vx_u8m1_b8(v, 'z', vl), vl);

        vbool8_t m_digit = __riscv_vmsgeu_vx_u8m1_b8(v, '0', vl);
        m_digit = __riscv_vmand_mm_b8(m_digit, __riscv_vmsleu_vx_u8m1_b8(v, '9', vl), vl);

        vbool8_t m_plus  = __riscv_vmseq_vx_u8m1_b8(v, '+', vl);
        vbool8_t m_slash = __riscv_vmseq_vx_u8m1_b8(v, '/', vl);

        /* Validate: every char must be in one of the 5 ranges */
        vbool8_t m_valid = __riscv_vmor_mm_b8(m_upper, m_lower, vl);
        m_valid = __riscv_vmor_mm_b8(m_valid, m_digit, vl);
        m_valid = __riscv_vmor_mm_b8(m_valid, m_plus, vl);
        m_valid = __riscv_vmor_mm_b8(m_valid, m_slash, vl);

        if (__riscv_vcpop_m_b8(m_valid, vl) != 16) {
            break;  /* Invalid char -> scalar fallback */
        }

        /* Compute decoded values per range */
        vuint8m1_t v_upper_val = __riscv_vsub_vx_u8m1(v, 'A', vl);
        vuint8m1_t v_lower_val = __riscv_vsub_vx_u8m1(v, 'a', vl);
        v_lower_val = __riscv_vadd_vx_u8m1(v_lower_val, 26, vl);
        vuint8m1_t v_digit_val = __riscv_vsub_vx_u8m1(v, '0', vl);
        v_digit_val = __riscv_vadd_vx_u8m1(v_digit_val, 52, vl);
        vuint8m1_t v_plus_val = __riscv_vmv_v_x_u8m1(62, vl);
        vuint8m1_t v_slash_val = __riscv_vmv_v_x_u8m1(63, vl);

        /* Merge chain: most specific first, least specific last */
        vuint8m1_t v_dec;
        v_dec = __riscv_vmerge_vvm_u8m1(v_slash_val, v_plus_val, m_plus, vl);
        v_dec = __riscv_vmerge_vvm_u8m1(v_dec, v_digit_val, m_digit, vl);
        v_dec = __riscv_vmerge_vvm_u8m1(v_dec, v_lower_val, m_lower, vl);
        v_dec = __riscv_vmerge_vvm_u8m1(v_dec, v_upper_val, m_upper, vl);

        /* Scalar packing: 4x6-bit -> 3 bytes */
        uint8_t dec_buf[16] __attribute__((aligned(16)));
        __riscv_vse8_v_u8m1(dec_buf, v_dec, vl);

        for (int j = 0; j < 4; j++) {
            uint8_t d0 = dec_buf[j*4];
            uint8_t d1 = dec_buf[j*4 + 1];
            uint8_t d2 = dec_buf[j*4 + 2];
            uint8_t d3 = dec_buf[j*4 + 3];
            *p++ = (uint8_t)((d0 << 2) | (d1 >> 4));
            *p++ = (uint8_t)(((d1 & 0x0F) << 4) | (d2 >> 2));
            *p++ = (uint8_t)(((d2 & 0x03) << 6) | d3);
        }
        i += 16;
    }

    /* Scalar fallback for remaining <16 bytes or chunks with errors */
    {
        size_t remaining = len - i;
        const uint8_t* s = (const uint8_t*)(src + i);
        uint8_t* d = p;

        for (size_t si = 0; si + 4 <= remaining; si += 4) {
            uint8_t c0 = s[si], c1 = s[si+1], c2 = s[si+2], c3 = s[si+3];
            int d0, d1, d2, d3;

#define DECODE_CHAR(c, val)                                                       do {                                                                      if (c >= 'A' && c <= 'Z')       val = c - 'A';                       else if (c >= 'a' && c <= 'z')  val = c - 'a' + 26;                  else if (c >= '0' && c <= '9')  val = c - '0' + 52;                  else if (c == '+')              val = 62;                             else if (c == '/')              val = 63;                             else return MODP_B64_RVV_ERROR;                                   } while (0)

            DECODE_CHAR(c0, d0); DECODE_CHAR(c1, d1);
            DECODE_CHAR(c2, d2); DECODE_CHAR(c3, d3);

#undef DECODE_CHAR

            *d++ = (uint8_t)((d0 << 2) | (d1 >> 4));
            *d++ = (uint8_t)(((d1 & 0x0F) << 4) | (d2 >> 2));
            *d++ = (uint8_t)(((d2 & 0x03) << 6) | d3);
        }
        return (size_t)(d - (uint8_t*)dest);
    }
}

#ifdef __cplusplus
}
#endif

#endif /* __riscv_vector */
