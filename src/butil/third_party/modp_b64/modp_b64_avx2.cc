// The AVX2 routines in this file are derived from aklomp/base64.
// Copyright (c) 2005-2007, Nick Galbreath
// Copyright (c) 2015-2018, Wojciech Muła
// Copyright (c) 2016-2017, Matthieu Darbois
// Copyright (c) 2013-2022, Alfred Klomp
// SPDX-License-Identifier: BSD-2-Clause

#include "modp_b64_avx2.h"

#if MODP_B64_HAS_AVX2

#include <immintrin.h>
#include <stdint.h>

namespace {

#define MODP_B64_AVX2_TARGET __attribute__((target("avx2")))
#define BASE64_FORCE_INLINE MODP_B64_AVX2_TARGET inline

const char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static BASE64_FORCE_INLINE __m256i
enc_reshuffle(const __m256i input) {
    const __m256i in = _mm256_shuffle_epi8(input, _mm256_set_epi8(
        10, 11, 9, 10, 7, 8, 6, 7, 4, 5, 3, 4, 1, 2, 0, 1,
        14, 15, 13, 14, 11, 12, 10, 11, 8, 9, 7, 8, 5, 6, 4, 5));
    const __m256i t0 = _mm256_and_si256(in, _mm256_set1_epi32(0x0FC0FC00));
    const __m256i t1 = _mm256_mulhi_epu16(t0, _mm256_set1_epi32(0x04000040));
    const __m256i t2 = _mm256_and_si256(in, _mm256_set1_epi32(0x003F03F0));
    const __m256i t3 = _mm256_mullo_epi16(t2, _mm256_set1_epi32(0x01000010));
    return _mm256_or_si256(t1, t3);
}

static BASE64_FORCE_INLINE __m256i
enc_translate(const __m256i in) {
    const __m256i lut = _mm256_setr_epi8(
        65, 71, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -19, -16, 0, 0,
        65, 71, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -19, -16, 0, 0);
    __m256i indices = _mm256_subs_epu8(in, _mm256_set1_epi8(51));
    const __m256i mask = _mm256_cmpgt_epi8(in, _mm256_set1_epi8(25));
    indices = _mm256_sub_epi8(indices, mask);
    return _mm256_add_epi8(in, _mm256_shuffle_epi8(lut, indices));
}

static BASE64_FORCE_INLINE __m256i
dec_reshuffle(const __m256i in) {
    const __m256i merge_ab_and_bc =
        _mm256_maddubs_epi16(in, _mm256_set1_epi32(0x01400140));
    __m256i out =
        _mm256_madd_epi16(merge_ab_and_bc, _mm256_set1_epi32(0x00011000));
    out = _mm256_shuffle_epi8(out, _mm256_setr_epi8(
        2, 1, 0, 6, 5, 4, 10, 9, 8, 14, 13, 12, -1, -1, -1, -1,
        2, 1, 0, 6, 5, 4, 10, 9, 8, 14, 13, 12, -1, -1, -1, -1));
    return _mm256_permutevar8x32_epi32(
        out, _mm256_setr_epi32(0, 1, 2, 4, 5, 6, -1, -1));
}

inline int DecodeChar(uint8_t c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

inline bool DecodeQuad(const uint8_t* src, uint8_t* dest) {
    const int a = DecodeChar(src[0]);
    const int b = DecodeChar(src[1]);
    const int c = DecodeChar(src[2]);
    const int d = DecodeChar(src[3]);
    if (a < 0 || b < 0 || c < 0 || d < 0) return false;
    dest[0] = static_cast<uint8_t>((a << 2) | (b >> 4));
    dest[1] = static_cast<uint8_t>(((b & 15) << 4) | (c >> 2));
    dest[2] = static_cast<uint8_t>(((c & 3) << 6) | d);
    return true;
}

}  // namespace

extern "C" int modp_b64_avx2_supported(void) {
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2");
}

extern "C" MODP_B64_AVX2_TARGET size_t modp_b64_avx2_encode(
    char* dest, const char* src, size_t len) {
    size_t input = 0;
    uint8_t* output = reinterpret_cast<uint8_t*>(dest);
    size_t rounds = len >= 32 ? (len - 4) / 24 : 0;
    if (rounds != 0) {
        // The first shuffle needs four zero bytes at the start of its lower
        // lane. This is the upstream AVX2 codec's safe first-block handling.
        __m256i block = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(src));
        block = _mm256_permutevar8x32_epi32(
            block, _mm256_setr_epi32(0, 0, 1, 2, 3, 4, 5, 6));
        block = enc_translate(enc_reshuffle(block));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(output), block);
        input = 20;
        output += 32;
        --rounds;
    }
    while (rounds != 0) {
        __m256i block = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(src + input));
        block = enc_translate(enc_reshuffle(block));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(output), block);
        input += 24;
        output += 32;
        --rounds;
    }
    if (input != 0) {
        input += 4;
    }

    while (input + 3 <= len) {
        const uint8_t a = static_cast<uint8_t>(src[input++]);
        const uint8_t b = static_cast<uint8_t>(src[input++]);
        const uint8_t c = static_cast<uint8_t>(src[input++]);
        *output++ = kAlphabet[a >> 2];
        *output++ = kAlphabet[((a & 3) << 4) | (b >> 4)];
        *output++ = kAlphabet[((b & 15) << 2) | (c >> 6)];
        *output++ = kAlphabet[c & 63];
    }
    if (input != len) {
        const uint8_t a = static_cast<uint8_t>(src[input++]);
        *output++ = kAlphabet[a >> 2];
        if (input == len) {
            *output++ = kAlphabet[(a & 3) << 4];
            *output++ = '=';
            *output++ = '=';
        } else {
            const uint8_t b = static_cast<uint8_t>(src[input]);
            *output++ = kAlphabet[((a & 3) << 4) | (b >> 4)];
            *output++ = kAlphabet[(b & 15) << 2];
            *output++ = '=';
        }
    }
    *output = '\0';
    return output - reinterpret_cast<uint8_t*>(dest);
}

extern "C" MODP_B64_AVX2_TARGET size_t modp_b64_avx2_decode(
    char* dest, const char* src, size_t len) {
    const size_t kError = static_cast<size_t>(-1);
    if (len == 0) return 0;
    if (len < 4 || len % 4 != 0) return kError;
    if (src[len - 1] == '=') {
        --len;
        if (src[len - 1] == '=') --len;
    }

    size_t input = 0;
    uint8_t* output = reinterpret_cast<uint8_t*>(dest);
    // Adapted from aklomp/base64/lib/arch/avx2/dec_loop.c. Keep thirteen
    // bytes for the scalar tail because the vector store writes 32 bytes.
    const __m256i lut_lo = _mm256_setr_epi8(
        0x15, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
        0x11, 0x11, 0x13, 0x1A, 0x1B, 0x1B, 0x1B, 0x1A,
        0x15, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
        0x11, 0x11, 0x13, 0x1A, 0x1B, 0x1B, 0x1B, 0x1A);
    const __m256i lut_hi = _mm256_setr_epi8(
        0x10, 0x10, 0x01, 0x02, 0x04, 0x08, 0x04, 0x08,
        0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
        0x10, 0x10, 0x01, 0x02, 0x04, 0x08, 0x04, 0x08,
        0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10);
    const __m256i lut_roll = _mm256_setr_epi8(
        0, 16, 19, 4, -65, -65, -71, -71, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 16, 19, 4, -65, -65, -71, -71, 0, 0, 0, 0, 0, 0, 0, 0);
    const __m256i mask_2f = _mm256_set1_epi8(0x2F);
    size_t rounds = len >= 45 ? (len - 13) / 32 : 0;
    while (rounds != 0) {
        __m256i block = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(src + input));
        const __m256i hi_nibbles = _mm256_and_si256(
            _mm256_srli_epi32(block, 4), mask_2f);
        const __m256i lo_nibbles = _mm256_and_si256(block, mask_2f);
        const __m256i hi = _mm256_shuffle_epi8(lut_hi, hi_nibbles);
        const __m256i lo = _mm256_shuffle_epi8(lut_lo, lo_nibbles);
        if (!_mm256_testz_si256(lo, hi)) return kError;
        const __m256i eq_2f = _mm256_cmpeq_epi8(block, mask_2f);
        const __m256i roll = _mm256_shuffle_epi8(
            lut_roll, _mm256_add_epi8(eq_2f, hi_nibbles));
        block = dec_reshuffle(_mm256_add_epi8(block, roll));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(output), block);
        input += 32;
        output += 24;
        --rounds;
    }

    while (input + 4 <= len) {
        if (!DecodeQuad(reinterpret_cast<const uint8_t*>(src + input), output)) {
            return kError;
        }
        input += 4;
        output += 3;
    }
    const size_t remaining = len - input;
    if (remaining == 2 || remaining == 3) {
        const int a = DecodeChar(static_cast<uint8_t>(src[input]));
        const int b = DecodeChar(static_cast<uint8_t>(src[input + 1]));
        if (a < 0 || b < 0) return kError;
        *output++ = static_cast<uint8_t>((a << 2) | (b >> 4));
        if (remaining == 3) {
            const int c = DecodeChar(static_cast<uint8_t>(src[input + 2]));
            if (c < 0) return kError;
            *output++ = static_cast<uint8_t>(((b & 15) << 4) | (c >> 2));
        }
    } else if (remaining != 0) {
        return kError;
    }
    return output - reinterpret_cast<uint8_t*>(dest);
}

#endif  /* MODP_B64_HAS_AVX2 */
