// MurmurHash3 was written by Austin Appleby, and is placed in the public
// domain. The author hereby disclaims copyright to this source code.

// Copyright (c) 2013 Baidu, Inc.
// Compute murmurhash3 iteratively so that you don't have to buffer
// everything in memory before computation. The APIs are similar with MD5

// Note - The x86 and x64 versions do _not_ produce the same results, as the
// algorithms are optimized for their respective platforms. You can still
// compile and run any of them on any platform, but your performance with the
// non-native version will be less than optimal.

#include <stddef.h>  // size_t
#include <string.h>  // memcpy
#include <algorithm> // std::min
#include "butil/third_party/murmurhash3/murmurhash3.h"

// Too many fallthroughs in this file to mark, just ignore the warning.
#if defined(__GNUC__) && __GNUC__ >= 7
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif

//-----------------------------------------------------------------------------
// Platform-specific functions and macros

// Microsoft Visual Studio

#if defined(_MSC_VER)

#include <stdlib.h>

#define ROTL32(x,y) _rotl(x,y)
#define ROTL64(x,y) _rotl64(x,y)

// Other compilers

#else   // defined(_MSC_VER)

inline uint32_t rotl32 ( uint32_t x, int8_t r )
{
  return (x << r) | (x >> (32 - r));
}

inline uint64_t rotl64 ( uint64_t x, int8_t r )
{
  return (x << r) | (x >> (64 - r));
}

#define ROTL32(x,y) rotl32(x,y)
#define ROTL64(x,y) rotl64(x,y)

#endif // !defined(_MSC_VER)

namespace butil {
//-----------------------------------------------------------------------------
// Block read - if your platform needs to do endian-swapping or can only
// handle aligned reads, do the conversion here

MURMURHASH_FORCE_INLINE uint32_t getblock32 ( const uint32_t * p, int i ) {
  return p[i];
}

MURMURHASH_FORCE_INLINE uint64_t getblock64 ( const uint64_t * p, int i ) {
  return p[i];
}

//-----------------------------------------------------------------------------

void MurmurHash3_x86_32 ( const void * key, int len,
                          uint32_t seed, void * out ) {
  const uint8_t * data = (const uint8_t*)key;
  const int nblocks = len / 4;

  uint32_t h1 = seed;

  const uint32_t c1 = 0xcc9e2d51;
  const uint32_t c2 = 0x1b873593;

  //----------
  // body

  const uint32_t * blocks = (const uint32_t *)(data + nblocks*4);

  for(int i = -nblocks; i; i++)
  {
    uint32_t k1 = getblock32(blocks,i);

    k1 *= c1;
    k1 = ROTL32(k1,15);
    k1 *= c2;
    
    h1 ^= k1;
    h1 = ROTL32(h1,13); 
    h1 = h1*5+0xe6546b64;
  }

  //----------
  // tail

  const uint8_t * tail = (const uint8_t*)(data + nblocks*4);

  uint32_t k1 = 0;

  switch(len & 3)
  {
  case 3: k1 ^= tail[2] << 16;
  case 2: k1 ^= tail[1] << 8;
  case 1: k1 ^= tail[0];
          k1 *= c1; k1 = ROTL32(k1,15); k1 *= c2; h1 ^= k1;
  };

  //----------
  // finalization

  h1 ^= len;

  h1 = fmix32(h1);

  *(uint32_t*)out = h1;
} 

//-----------------------------------------------------------------------------

void MurmurHash3_x86_128 ( const void * key, const int len,
                           uint32_t seed, void * out ) {
  const uint8_t * data = (const uint8_t*)key;
  const int nblocks = len / 16;

  uint32_t h1 = seed;
  uint32_t h2 = seed;
  uint32_t h3 = seed;
  uint32_t h4 = seed;

  const uint32_t c1 = 0x239b961b; 
  const uint32_t c2 = 0xab0e9789;
  const uint32_t c3 = 0x38b34ae5; 
  const uint32_t c4 = 0xa1e38b93;

  //----------
  // body

  const uint32_t * blocks = (const uint32_t *)(data + nblocks*16);

  for(int i = -nblocks; i; i++)
  {
    uint32_t k1 = getblock32(blocks,i*4+0);
    uint32_t k2 = getblock32(blocks,i*4+1);
    uint32_t k3 = getblock32(blocks,i*4+2);
    uint32_t k4 = getblock32(blocks,i*4+3);

    k1 *= c1; k1  = ROTL32(k1,15); k1 *= c2; h1 ^= k1;

    h1 = ROTL32(h1,19); h1 += h2; h1 = h1*5+0x561ccd1b;

    k2 *= c2; k2  = ROTL32(k2,16); k2 *= c3; h2 ^= k2;

    h2 = ROTL32(h2,17); h2 += h3; h2 = h2*5+0x0bcaa747;

    k3 *= c3; k3  = ROTL32(k3,17); k3 *= c4; h3 ^= k3;

    h3 = ROTL32(h3,15); h3 += h4; h3 = h3*5+0x96cd1c35;

    k4 *= c4; k4  = ROTL32(k4,18); k4 *= c1; h4 ^= k4;

    h4 = ROTL32(h4,13); h4 += h1; h4 = h4*5+0x32ac3b17;
  }

  //----------
  // tail

  const uint8_t * tail = (const uint8_t*)(data + nblocks*16);

  uint32_t k1 = 0;
  uint32_t k2 = 0;
  uint32_t k3 = 0;
  uint32_t k4 = 0;

  switch(len & 15)
  {
  case 15: k4 ^= tail[14] << 16;
  case 14: k4 ^= tail[13] << 8;
  case 13: k4 ^= tail[12] << 0;
           k4 *= c4; k4  = ROTL32(k4,18); k4 *= c1; h4 ^= k4;

  case 12: k3 ^= tail[11] << 24;
  case 11: k3 ^= tail[10] << 16;
  case 10: k3 ^= tail[ 9] << 8;
  case  9: k3 ^= tail[ 8] << 0;
           k3 *= c3; k3  = ROTL32(k3,17); k3 *= c4; h3 ^= k3;

  case  8: k2 ^= tail[ 7] << 24;
  case  7: k2 ^= tail[ 6] << 16;
  case  6: k2 ^= tail[ 5] << 8;
  case  5: k2 ^= tail[ 4] << 0;
           k2 *= c2; k2  = ROTL32(k2,16); k2 *= c3; h2 ^= k2;

  case  4: k1 ^= tail[ 3] << 24;
  case  3: k1 ^= tail[ 2] << 16;
  case  2: k1 ^= tail[ 1] << 8;
  case  1: k1 ^= tail[ 0] << 0;
           k1 *= c1; k1  = ROTL32(k1,15); k1 *= c2; h1 ^= k1;
  };

  //----------
  // finalization

  h1 ^= len; h2 ^= len; h3 ^= len; h4 ^= len;

  h1 += h2; h1 += h3; h1 += h4;
  h2 += h1; h3 += h1; h4 += h1;

  h1 = fmix32(h1);
  h2 = fmix32(h2);
  h3 = fmix32(h3);
  h4 = fmix32(h4);

  h1 += h2; h1 += h3; h1 += h4;
  h2 += h1; h3 += h1; h4 += h1;

  ((uint32_t*)out)[0] = h1;
  ((uint32_t*)out)[1] = h2;
  ((uint32_t*)out)[2] = h3;
  ((uint32_t*)out)[3] = h4;
}

//-----------------------------------------------------------------------------

void MurmurHash3_x64_128 ( const void * key, const int len,
                           const uint32_t seed, void * out ) {
  const uint8_t * data = (const uint8_t*)key;
  const int nblocks = len / 16;

  uint64_t h1 = seed;
  uint64_t h2 = seed;

  const uint64_t c1 = BIG_CONSTANT(0x87c37b91114253d5);
  const uint64_t c2 = BIG_CONSTANT(0x4cf5ad432745937f);

  //----------
  // body

  const uint64_t * blocks = (const uint64_t *)(data);

  for(int i = 0; i < nblocks; i++)
  {
    uint64_t k1 = getblock64(blocks,i*2+0);
    uint64_t k2 = getblock64(blocks,i*2+1);

    k1 *= c1; k1  = ROTL64(k1,31); k1 *= c2; h1 ^= k1;

    h1 = ROTL64(h1,27); h1 += h2; h1 = h1*5+0x52dce729;

    k2 *= c2; k2  = ROTL64(k2,33); k2 *= c1; h2 ^= k2;

    h2 = ROTL64(h2,31); h2 += h1; h2 = h2*5+0x38495ab5;
  }

  //----------
  // tail

  const uint8_t * tail = (const uint8_t*)(data + nblocks*16);

  uint64_t k1 = 0;
  uint64_t k2 = 0;

  switch(len & 15)
  {
  case 15: k2 ^= ((uint64_t)tail[14]) << 48;
  case 14: k2 ^= ((uint64_t)tail[13]) << 40;
  case 13: k2 ^= ((uint64_t)tail[12]) << 32;
  case 12: k2 ^= ((uint64_t)tail[11]) << 24;
  case 11: k2 ^= ((uint64_t)tail[10]) << 16;
  case 10: k2 ^= ((uint64_t)tail[ 9]) << 8;
  case  9: k2 ^= ((uint64_t)tail[ 8]) << 0;
           k2 *= c2; k2  = ROTL64(k2,33); k2 *= c1; h2 ^= k2;

  case  8: k1 ^= ((uint64_t)tail[ 7]) << 56;
  case  7: k1 ^= ((uint64_t)tail[ 6]) << 48;
  case  6: k1 ^= ((uint64_t)tail[ 5]) << 40;
  case  5: k1 ^= ((uint64_t)tail[ 4]) << 32;
  case  4: k1 ^= ((uint64_t)tail[ 3]) << 24;
  case  3: k1 ^= ((uint64_t)tail[ 2]) << 16;
  case  2: k1 ^= ((uint64_t)tail[ 1]) << 8;
  case  1: k1 ^= ((uint64_t)tail[ 0]) << 0;
           k1 *= c1; k1  = ROTL64(k1,31); k1 *= c2; h1 ^= k1;
  };

  //----------
  // finalization

  h1 ^= len; h2 ^= len;

  h1 += h2;
  h2 += h1;

  h1 = fmix64(h1);
  h2 = fmix64(h2);

  h1 += h2;
  h2 += h1;

  ((uint64_t*)out)[0] = h1;
  ((uint64_t*)out)[1] = h2;
}

// ============= iterative versions ==================

void MurmurHash3_x86_128_Init(MurmurHash3_x86_128_Context* ctx, uint32_t seed)
{
    ctx->h1 = seed;
    ctx->h2 = seed;
    ctx->h3 = seed;
    ctx->h4 = seed;
    ctx->total_len = 0;
    ctx->tail_len = 0;
}

void MurmurHash3_x86_128_Update(
    MurmurHash3_x86_128_Context* ctx, const void * key, int len)
{
    const uint8_t * data = (const uint8_t*)key;

    uint32_t h1 = ctx->h1;
    uint32_t h2 = ctx->h2;
    uint32_t h3 = ctx->h3;
    uint32_t h4 = ctx->h4;

    const uint32_t c1 = 0x239b961b; 
    const uint32_t c2 = 0xab0e9789;
    const uint32_t c3 = 0x38b34ae5; 
    const uint32_t c4 = 0xa1e38b93;

    if (ctx->tail_len > 0) {
        const int append = std::min(len, 16 - ctx->tail_len);
        memcpy(ctx->tail + ctx->tail_len, data, append);
        ctx->total_len += append;
        ctx->tail_len += append;
        data += append;
        len -= append;
        if (ctx->tail_len == 16) {
            uint32_t k1 = getblock32((uint32_t*)ctx->tail, 0);
            uint32_t k2 = getblock32((uint32_t*)ctx->tail, 1);
            uint32_t k3 = getblock32((uint32_t*)ctx->tail, 2);
            uint32_t k4 = getblock32((uint32_t*)ctx->tail, 3);

            k1 *= c1; k1  = ROTL32(k1,15); k1 *= c2; h1 ^= k1;

            h1 = ROTL32(h1,19); h1 += h2; h1 = h1*5+0x561ccd1b;

            k2 *= c2; k2  = ROTL32(k2,16); k2 *= c3; h2 ^= k2;

            h2 = ROTL32(h2,17); h2 += h3; h2 = h2*5+0x0bcaa747;

            k3 *= c3; k3  = ROTL32(k3,17); k3 *= c4; h3 ^= k3;

            h3 = ROTL32(h3,15); h3 += h4; h3 = h3*5+0x96cd1c35;

            k4 *= c4; k4  = ROTL32(k4,18); k4 *= c1; h4 ^= k4;

            h4 = ROTL32(h4,13); h4 += h1; h4 = h4*5+0x32ac3b17;

            ctx->tail_len = 0;
        }
    }

    const int nblocks = len / 16;

    //----------
    // body

    const uint32_t * blocks = (const uint32_t *)(data + nblocks*16);

    for (int i = -nblocks; i; i++)
    {
        uint32_t k1 = getblock32(blocks,i*4+0);
        uint32_t k2 = getblock32(blocks,i*4+1);
        uint32_t k3 = getblock32(blocks,i*4+2);
        uint32_t k4 = getblock32(blocks,i*4+3);

        k1 *= c1; k1  = ROTL32(k1,15); k1 *= c2; h1 ^= k1;

        h1 = ROTL32(h1,19); h1 += h2; h1 = h1*5+0x561ccd1b;

        k2 *= c2; k2  = ROTL32(k2,16); k2 *= c3; h2 ^= k2;

        h2 = ROTL32(h2,17); h2 += h3; h2 = h2*5+0x0bcaa747;

        k3 *= c3; k3  = ROTL32(k3,17); k3 *= c4; h3 ^= k3;

        h3 = ROTL32(h3,15); h3 += h4; h3 = h3*5+0x96cd1c35;

        k4 *= c4; k4  = ROTL32(k4,18); k4 *= c1; h4 ^= k4;

        h4 = ROTL32(h4,13); h4 += h1; h4 = h4*5+0x32ac3b17;
    }

    //----------
    // tail

    const int tail_len = len & 15;
    if (tail_len > 0) {
        memcpy(ctx->tail, data + nblocks * 16, tail_len);
        ctx->tail_len = tail_len;
    }
    ctx->h1 = h1;
    ctx->h2 = h2;
    ctx->h3 = h3;
    ctx->h4 = h4;
    ctx->total_len += len;
}

void MurmurHash3_x86_128_Final(void * out, const MurmurHash3_x86_128_Context* ctx)
{
    uint32_t h1 = ctx->h1;
    uint32_t h2 = ctx->h2;
    uint32_t h3 = ctx->h3;
    uint32_t h4 = ctx->h4;
    const int len = ctx->total_len;

    const uint8_t * tail = ctx->tail;

    const uint32_t c1 = 0x239b961b; 
    const uint32_t c2 = 0xab0e9789;
    const uint32_t c3 = 0x38b34ae5; 
    const uint32_t c4 = 0xa1e38b93;

    uint32_t k1 = 0;
    uint32_t k2 = 0;
    uint32_t k3 = 0;
    uint32_t k4 = 0;

    switch (ctx->tail_len)
    {
    case 15: k4 ^= tail[14] << 16;
    case 14: k4 ^= tail[13] << 8;
    case 13: k4 ^= tail[12] << 0;
        k4 *= c4; k4  = ROTL32(k4,18); k4 *= c1; h4 ^= k4;

    case 12: k3 ^= tail[11] << 24;
    case 11: k3 ^= tail[10] << 16;
    case 10: k3 ^= tail[ 9] << 8;
    case  9: k3 ^= tail[ 8] << 0;
        k3 *= c3; k3  = ROTL32(k3,17); k3 *= c4; h3 ^= k3;

    case  8: k2 ^= tail[ 7] << 24;
    case  7: k2 ^= tail[ 6] << 16;
    case  6: k2 ^= tail[ 5] << 8;
    case  5: k2 ^= tail[ 4] << 0;
        k2 *= c2; k2  = ROTL32(k2,16); k2 *= c3; h2 ^= k2;

    case  4: k1 ^= tail[ 3] << 24;
    case  3: k1 ^= tail[ 2] << 16;
    case  2: k1 ^= tail[ 1] << 8;
    case  1: k1 ^= tail[ 0] << 0;
        k1 *= c1; k1  = ROTL32(k1,15); k1 *= c2; h1 ^= k1;
    };


    //----------
    // finalization

    h1 ^= len; h2 ^= len; h3 ^= len; h4 ^= len;

    h1 += h2; h1 += h3; h1 += h4;
    h2 += h1; h3 += h1; h4 += h1;

    h1 = fmix32(h1);
    h2 = fmix32(h2);
    h3 = fmix32(h3);
    h4 = fmix32(h4);

    h1 += h2; h1 += h3; h1 += h4;
    h2 += h1; h3 += h1; h4 += h1;

    ((uint32_t*)out)[0] = h1;
    ((uint32_t*)out)[1] = h2;
    ((uint32_t*)out)[2] = h3;
    ((uint32_t*)out)[3] = h4;
}

//-----------------------------------------------------------------------------

void MurmurHash3_x64_128_Init(MurmurHash3_x64_128_Context* ctx, uint32_t seed)
{
    ctx->h1 = seed;
    ctx->h2 = seed;
    ctx->total_len = 0;
    ctx->tail_len = 0;
}

void MurmurHash3_x64_128_Update(
    MurmurHash3_x64_128_Context* ctx, const void * key, int len)
{
    uint64_t h1 = ctx->h1;
    uint64_t h2 = ctx->h2;

    const uint64_t c1 = BIG_CONSTANT(0x87c37b91114253d5);
    const uint64_t c2 = BIG_CONSTANT(0x4cf5ad432745937f);

    const uint8_t * data = (const uint8_t*)key;
    if (ctx->tail_len > 0) {
        const int append = std::min(len, 16 - ctx->tail_len);
        memcpy(ctx->tail + ctx->tail_len, data, append);
        ctx->total_len += append;
        ctx->tail_len += append;
        data += append;
        len -= append;
        if (ctx->tail_len == 16) {
            uint64_t k1 = getblock64((uint64_t*)ctx->tail, 0);
            uint64_t k2 = getblock64((uint64_t*)ctx->tail, 1);

            k1 *= c1; k1  = ROTL64(k1,31); k1 *= c2; h1 ^= k1;

            h1 = ROTL64(h1,27); h1 += h2; h1 = h1*5+0x52dce729;

            k2 *= c2; k2  = ROTL64(k2,33); k2 *= c1; h2 ^= k2;

            h2 = ROTL64(h2,31); h2 += h1; h2 = h2*5+0x38495ab5;
            ctx->tail_len = 0;
        }
    }

    const int nblocks = len / 16;

    //----------
    // body

    const uint64_t * blocks = (const uint64_t *)(data);

    for (int i = 0; i < nblocks; i++)
    {
        uint64_t k1 = getblock64(blocks,i*2+0);
        uint64_t k2 = getblock64(blocks,i*2+1);

        k1 *= c1; k1  = ROTL64(k1,31); k1 *= c2; h1 ^= k1;

        h1 = ROTL64(h1,27); h1 += h2; h1 = h1*5+0x52dce729;

        k2 *= c2; k2  = ROTL64(k2,33); k2 *= c1; h2 ^= k2;

        h2 = ROTL64(h2,31); h2 += h1; h2 = h2*5+0x38495ab5;
    }

    // tail
    const int tail_len = len & 15;
    if (tail_len > 0) {
        memcpy(ctx->tail, data + nblocks * 16, tail_len);
        ctx->tail_len = tail_len;
    }

    ctx->h1 = h1;
    ctx->h2 = h2;
    ctx->total_len += len;
}

void MurmurHash3_x64_128_Final(void * out, const MurmurHash3_x64_128_Context* ctx)
{
    uint64_t h1 = ctx->h1;
    uint64_t h2 = ctx->h2;
    const uint64_t len = ctx->total_len;
    //----------
    // tail

    const uint8_t * tail = ctx->tail;
    const uint64_t c1 = BIG_CONSTANT(0x87c37b91114253d5);
    const uint64_t c2 = BIG_CONSTANT(0x4cf5ad432745937f);

    uint64_t k1 = 0;
    uint64_t k2 = 0;

    switch (ctx->tail_len)
    {
    case 15: k2 ^= ((uint64_t)tail[14]) << 48;
    case 14: k2 ^= ((uint64_t)tail[13]) << 40;
    case 13: k2 ^= ((uint64_t)tail[12]) << 32;
    case 12: k2 ^= ((uint64_t)tail[11]) << 24;
    case 11: k2 ^= ((uint64_t)tail[10]) << 16;
    case 10: k2 ^= ((uint64_t)tail[ 9]) << 8;
    case  9: k2 ^= ((uint64_t)tail[ 8]) << 0;
        k2 *= c2; k2  = ROTL64(k2,33); k2 *= c1; h2 ^= k2;

    case  8: k1 ^= ((uint64_t)tail[ 7]) << 56;
    case  7: k1 ^= ((uint64_t)tail[ 6]) << 48;
    case  6: k1 ^= ((uint64_t)tail[ 5]) << 40;
    case  5: k1 ^= ((uint64_t)tail[ 4]) << 32;
    case  4: k1 ^= ((uint64_t)tail[ 3]) << 24;
    case  3: k1 ^= ((uint64_t)tail[ 2]) << 16;
    case  2: k1 ^= ((uint64_t)tail[ 1]) << 8;
    case  1: k1 ^= ((uint64_t)tail[ 0]) << 0;
        k1 *= c1; k1  = ROTL64(k1,31); k1 *= c2; h1 ^= k1;
    };


    //----------
    // finalization

    h1 ^= len; h2 ^= len;

    h1 += h2;
    h2 += h1;

    h1 = fmix64(h1);
    h2 = fmix64(h2);

    h1 += h2;
    h2 += h1;

    ((uint64_t*)out)[0] = h1;
    ((uint64_t*)out)[1] = h2;
}

void MurmurHash3_x86_32_Init(MurmurHash3_x86_32_Context* ctx, uint32_t seed) {
    ctx->h1 = seed;
    ctx->total_len = 0;
    ctx->tail_len = 0;
}

void MurmurHash3_x86_32_Update(MurmurHash3_x86_32_Context* ctx, const void* key, int len) {
    
    uint32_t h1 = ctx->h1;
    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;
    const uint8_t * data = (const uint8_t*)key;
    if (ctx->tail_len > 0) {
        const int append = std::min(len, 4 - ctx->tail_len);
        memcpy(ctx->tail + ctx->tail_len, data, append);
        ctx->total_len += append;
        ctx->tail_len += append;
        data += append;
        len -= append;
        if (ctx->tail_len == 4) {
            uint32_t k1 = getblock32((uint32_t*)ctx->tail,0);

            k1 *= c1;
            k1 = ROTL32(k1,15);
            k1 *= c2;

            h1 ^= k1;
            h1 = ROTL32(h1,13); 
            h1 = h1*5+0xe6546b64;
            ctx->tail_len = 0;
        }
    }

    const int nblocks = len / 4;

    //----------
    // body

    const uint32_t * blocks = (const uint32_t *)(data + nblocks*4);

    for (int i = -nblocks; i; i++)
    {
        uint32_t k1 = getblock32(blocks,i);

        k1 *= c1;
        k1 = ROTL32(k1,15);
        k1 *= c2;

        h1 ^= k1;
        h1 = ROTL32(h1,13); 
        h1 = h1*5+0xe6546b64;
    }

    //----------
    // tail

    const int tail_len = len & 3;
    if (tail_len > 0) {
        memcpy(ctx->tail, data + nblocks * 4, tail_len);
        ctx->tail_len = tail_len;
    }

    //----------
    // finalization
    ctx->h1 = h1;
    ctx->total_len += len;
}

void MurmurHash3_x86_32_Final(void * out, const MurmurHash3_x86_32_Context* ctx) {
    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;
    uint32_t h1 = ctx->h1;

    uint32_t k1 = 0;
    const uint8_t* tail = ctx->tail; 
    switch (ctx->tail_len)
    {
    case 3: k1 ^= tail[2] << 16;
    case 2: k1 ^= tail[1] << 8;
    case 1: k1 ^= tail[0];
            k1 *= c1; k1 = ROTL32(k1,15); k1 *= c2; h1 ^= k1;
    };

    h1 ^= ctx->total_len;
    h1 = fmix32(h1);
    *(uint32_t*)out = h1;
}

} // namespace butil
