// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This header defines cross-platform ByteSwap() implementations for 16, 32 and
// 64-bit values, and NetToHostXX() / HostToNextXX() functions equivalent to
// the traditional ntohX() and htonX() functions.
// Use the functions defined here rather than using the platform-specific
// functions directly.

#ifndef BUTIL_SYS_BYTEORDER_H_
#define BUTIL_SYS_BYTEORDER_H_

#include "butil/basictypes.h"
#include "butil/build_config.h"

#if defined(OS_WIN)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#if defined(COMPILER_MSVC)
#include <stdlib.h> // for _byteswap_*
#elif defined(OS_MACOSX)
// Mac OS X / Darwin features
#include <libkern/OSByteOrder.h> // for OSSwapInt*
#elif defined(OS_LINUX)
#include <byteswap.h> // for bswap_*
#endif

#if defined(ARCH_CPU_LITTLE_ENDIAN)
#include "butil/mysql_little_endian.h"
#else
#include "butil/mysql_big_endian.h"
#endif

namespace butil {

#if defined(COMPILER_MSVC)
inline uint16_t ByteSwap(uint16_t x) { return _byteswap_ushort(x); }
inline uint32_t ByteSwap(uint32_t x) { return _byteswap_ulong(x); }
inline uint64_t ByteSwap(uint64_t x) { return _byteswap_uint64(x); }

#elif defined(OS_MACOSX)
inline uint16_t ByteSwap(uint16_t x) { return OSSwapInt16(x); }
inline uint32_t ByteSwap(uint32_t x) { return OSSwapInt32(x); }
inline uint64_t ByteSwap(uint64_t x) { return OSSwapInt64(x); }

#elif defined(OS_LINUX)
inline uint16_t ByteSwap(uint16_t x) { return bswap_16(x); }
inline uint32_t ByteSwap(uint32_t x) { return bswap_32(x); }
inline uint64_t ByteSwap(uint64_t x) { return bswap_64(x); }

#else
// Returns a value with all bytes in |x| swapped, i.e. reverses the endianness.
inline uint16_t ByteSwap(uint16_t x) {
  return (x << 8) | (x >> 8);
}

inline uint32_t ByteSwap(uint32_t x) {
    x = ((x & 0xff00ff00UL) >> 8) | ((x & 0x00ff00ffUL) << 8);
    return (x >> 16) | (x << 16);
}

inline uint64_t ByteSwap(uint64_t x) {
    x = ((x & 0xff00ff00ff00ff00ULL) >> 8) | ((x & 0x00ff00ff00ff00ffULL) << 8);
    x = ((x & 0xffff0000ffff0000ULL) >> 16) | ((x & 0x0000ffff0000ffffULL) << 16);
    return (x >> 32) | (x << 32);
}
#endif

// Converts the bytes in |x| from host order (endianness) to little endian, and
// returns the result.
inline uint16_t ByteSwapToLE16(uint16_t x) {
#if defined(ARCH_CPU_LITTLE_ENDIAN)
  return x;
#else
  return ByteSwap(x);
#endif
}
inline uint32_t ByteSwapToLE32(uint32_t x) {
#if defined(ARCH_CPU_LITTLE_ENDIAN)
  return x;
#else
  return ByteSwap(x);
#endif
}
inline uint64_t ByteSwapToLE64(uint64_t x) {
#if defined(ARCH_CPU_LITTLE_ENDIAN)
  return x;
#else
  return ByteSwap(x);
#endif
}

// Converts the bytes in |x| from network to host order (endianness), and
// returns the result.
inline uint16_t NetToHost16(uint16_t x) {
#if defined(ARCH_CPU_LITTLE_ENDIAN)
  return ByteSwap(x);
#else
  return x;
#endif
}
inline uint32_t NetToHost32(uint32_t x) {
#if defined(ARCH_CPU_LITTLE_ENDIAN)
  return ByteSwap(x);
#else
  return x;
#endif
}
inline uint64_t NetToHost64(uint64_t x) {
#if defined(ARCH_CPU_LITTLE_ENDIAN)
  return ByteSwap(x);
#else
  return x;
#endif
}

// Converts the bytes in |x| from host to network order (endianness), and
// returns the result.
inline uint16_t HostToNet16(uint16_t x) {
#if defined(ARCH_CPU_LITTLE_ENDIAN)
  return ByteSwap(x);
#else
  return x;
#endif
}
inline uint32_t HostToNet32(uint32_t x) {
#if defined(ARCH_CPU_LITTLE_ENDIAN)
  return ByteSwap(x);
#else
  return x;
#endif
}
inline uint64_t HostToNet64(uint64_t x) {
#if defined(ARCH_CPU_LITTLE_ENDIAN)
  return ByteSwap(x);
#else
  return x;
#endif
}

// for mysql protocol
static inline int16_t SignedIntLoad2Bytes(const char* src) {
    return SignedIntLoad2Bytes(static_cast<const uint8_t*>(static_cast<const void*>(src)));
}

static inline uint16_t UnsignedIntLoad2Bytes(const char* src) {
    return UnsignedIntLoad2Bytes(static_cast<const uint8_t*>(static_cast<const void*>(src)));
}

static inline int32_t SignedIntLoad3Bytes(const char* src) {
    return SignedIntLoad3Bytes(static_cast<const uint8_t*>(static_cast<const void*>(src)));
}

static inline uint32_t UnsignedIntLoad3Bytes(const char* src) {
    return UnsignedIntLoad3Bytes(static_cast<const uint8_t*>(static_cast<const void*>(src)));
}

static inline int32_t SignedIntLoad4Bytes(const char* src) {
    return SignedIntLoad4Bytes(static_cast<const uint8_t*>(static_cast<const void*>(src)));
}

static inline uint32_t UnsignedIntLoad4Bytes(const char* src) {
    return UnsignedIntLoad4Bytes(static_cast<const uint8_t*>(static_cast<const void*>(src)));
}

static inline uint64_t UnsignedIntLoad6Bytes(const char* src) {
    return UnsignedIntLoad6Bytes(static_cast<const uint8_t*>(static_cast<const void*>(src)));
}

static inline uint64_t SignedIntLoad8Bytes(const char* src) {
    return SignedIntLoad8Bytes(static_cast<const uint8_t*>(static_cast<const void*>(src)));
}

static inline uint64_t UnsignedIntLoad8Bytes(const char* src) {
    return UnsignedIntLoad8Bytes(static_cast<const uint8_t*>(static_cast<const void*>(src)));
}

static inline void IntStore2Bytes(char* src, uint16_t num) {
    IntStore2Bytes(static_cast<uint8_t*>(static_cast<void*>(src)), num);
}

static inline void IntSotre3Bytes(char* src, uint32_t num) {
    IntStore3Bytes(static_cast<uint8_t*>(static_cast<void*>(src)), num);
}

static inline void IntStore4Bytes(char* src, uint32_t num) {
    IntStore4Bytes(static_cast<uint8_t*>(static_cast<void*>(src)), num);
}

static inline void IntStore5Bytes(char* src, uint64_t num) {
    IntStore5Bytes(static_cast<uint8_t*>(static_cast<void*>(src)), num);
}

static inline void IntStore6Bytes(char* src, uint64_t num) {
    IntStore6Bytes(static_cast<uint8_t*>(static_cast<void*>(src)), num);
}

static inline void IntStore8Bytes(char* src, uint64_t num) {
    IntStore8Bytes(static_cast<uint8_t*>(static_cast<void*>(src)), num);
}

}  // namespace butil

#endif  // BUTIL_SYS_BYTEORDER_H_
