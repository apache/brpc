// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This header defines cross-platform ByteSwap() implementations for 16, 32 and
// 64-bit values, and NetToHostXX() / HostToNextXX() functions equivalent to
// the traditional ntohX() and htonX() functions.
// Use the functions defined here rather than using the platform-specific
// functions directly.
//
// copy from mysql include files: my_byteorder.h, little_endian.h, big_endian.h

#ifndef BUTIL_MYSQL_LITTLE_ENDIAN_H
#define BUTIL_MYSQL_LITTLE_ENDIAN_H

#include "butil/basictypes.h"

namespace butil {

// Load functions
static inline int16_t SignedIntLoad2Bytes(const uint8_t* src) {
    int16_t ret;
    memcpy(&ret, src, sizeof(ret));
    return ret;
}

static inline int32_t SignedIntLoad4Bytes(const uint8_t* src) {
    int32_t ret;
    memcpy(&ret, src, sizeof(ret));
    return ret;
}

static inline int64_t SignedIntLoad8Bytes(const uint8_t* src) {
    int64_t ret;
    memcpy(&ret, src, sizeof(ret));
    return ret;
}

static inline uint16_t UnsignedIntLoad2Bytes(const uint8_t* src) {
    uint16_t ret;
    memcpy(&ret, src, sizeof(ret));
    return ret;
}

static inline uint32_t UnsignedIntLoad4Bytes(const uint8_t* src) {
    uint32_t ret;
    memcpy(&ret, src, sizeof(ret));
    return ret;
}

static inline uint64_t UnsignedIntLoad8Bytes(const uint8_t* src) {
    uint64_t ret;
    memcpy(&ret, src, sizeof(ret));
    return ret;
}

static inline int32_t SignedIntLoad3Bytes(const uint8_t* src) {
    return ((int32_t)(((src[2]) & 128)
                      ? (((uint32_t)255L << 24) | (((uint32_t)src[2]) << 16) |
                         (((uint32_t)src[1]) << 8) | ((uint32_t)src[0]))
                      : (((uint32_t)src[2]) << 16) | (((uint32_t)src[1]) << 8) |
                      ((uint32_t)src[0])));
}

static inline uint32_t UnsignedIntLoad3Bytes(const uint8_t* src) {
    return (uint32_t)(((uint32_t)(src[0])) + (((uint32_t)(src[1])) << 8) +
                      (((uint32_t)(src[2])) << 16));
}

static inline uint64_t UnsignedIntLoad5Bytes(const uint8_t* src) {
    return ((uint64_t)(((uint32_t)(src[0])) + (((uint32_t)(src[1])) << 8) +
                       (((uint32_t)(src[2])) << 16) + (((uint32_t)(src[3])) << 24)) +
            (((uint64_t)(src[4])) << 32));
}

static inline uint64_t UnsignedIntLoad6Bytes(const uint8_t* src) {
    return ((uint64_t)(((uint32_t)(src[0])) + (((uint32_t)(src[1])) << 8) +
                       (((uint32_t)(src[2])) << 16) + (((uint32_t)(src[3])) << 24)) +
            (((uint64_t)(src[4])) << 32) + (((uint64_t)(src[5])) << 40));
}

// Store functions

static inline void IntStore2Bytes(uint8_t* dst, uint16_t num) {
    memcpy(dst, &num, sizeof(num));
}

static inline void IntStore3Bytes(uint8_t* dst, uint32_t num) {
    *(dst) = (uint8_t)(num);
    *(dst + 1) = (uint8_t)(num >> 8);
    *(dst + 2) = (uint8_t)(num >> 16);
}

static inline void IntStore4Bytes(uint8_t* dst, uint32_t num) {
    memcpy(dst, &num, sizeof(num));
}

static inline void IntStore5Bytes(uint8_t* dst, uint64_t num) {
    *(dst) = (uint8_t)(num);
    *(dst + 1) = (uint8_t)(num >> 8);
    *(dst + 2) = (uint8_t)(num >> 16);
    *(dst + 3) = (uint8_t)(num >> 24);
    *(dst + 4) = (uint8_t)(num >> 32);
}

static inline void IntStore6Bytes(uint8_t* dst, uint64_t num) {
    *(dst) = (uint8_t)(num);
    *(dst + 1) = (uint8_t)(num >> 8);
    *(dst + 2) = (uint8_t)(num >> 16);
    *(dst + 3) = (uint8_t)(num >> 24);
    *(dst + 4) = (uint8_t)(num >> 32);
    *(dst + 5) = (uint8_t)(num >> 40);
}

static inline void IntStore7Bytes(uint8_t* dst, uint64_t num) {
    memcpy(dst, &num, 7);
}

static inline void IntStore8Bytes(uint8_t* dst, uint64_t num) {
    memcpy(dst, &num, sizeof(num));
}

}  // namespace butil

#endif  // BUTIL_MYSQL_LITTLE_ENDIAN_H
