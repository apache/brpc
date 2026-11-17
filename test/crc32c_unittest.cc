//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <gtest/gtest.h>
#include <vector>
#include "butil/crc32c.h"
#include "butil/time.h"
#if defined(__aarch64__) && defined(__linux__)
#include <sys/auxv.h>
#include <asm/hwcap.h>
#ifndef HWCAP_CRC32
#define HWCAP_CRC32 (1 << 7)
#endif
#endif

namespace butil {
namespace crc32c {

namespace {

uint32_t ReferenceExtend(uint32_t init_crc, const char* data, size_t n) {
  uint32_t c = init_crc ^ 0xffffffffU;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
  for (size_t i = 0; i < n; ++i) {
    c ^= p[i];
    for (int bit = 0; bit < 8; ++bit) {
      c = (c >> 1) ^ ((c & 1U) ? 0x82f63b78U : 0U);
    }
  }
  return c ^ 0xffffffffU;
}

}  // namespace

class CRC : public testing::Test { };

TEST_F(CRC, StandardResults) {
  // From rfc3720 section B.4.
  char buf[32];

  memset(buf, 0, sizeof(buf));
  ASSERT_EQ(0x8a9136aaU, Value(buf, sizeof(buf)));

  memset(buf, 0xff, sizeof(buf));
  ASSERT_EQ(0x62a8ab43U, Value(buf, sizeof(buf)));

  for (int i = 0; i < 32; i++) {
    buf[i] = i;
  }
  ASSERT_EQ(0x46dd794eU, Value(buf, sizeof(buf)));

  for (int i = 0; i < 32; i++) {
    buf[i] = 31 - i;
  }
  ASSERT_EQ(0x113fdb5cU, Value(buf, sizeof(buf)));

  unsigned char data[48] = {
    0x01, 0xc0, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x14, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x04, 0x00,
    0x00, 0x00, 0x00, 0x14,
    0x00, 0x00, 0x00, 0x18,
    0x28, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
  };
  ASSERT_EQ(0xd9963a56, Value(reinterpret_cast<char*>(data), sizeof(data)));
}

TEST_F(CRC, Values) {
  ASSERT_NE(Value("a", 1), Value("foo", 3));
}

TEST_F(CRC, Extend) {
  ASSERT_EQ(Value("hello world", 11),
            Extend(Value("hello ", 6), "world", 5));
}

TEST_F(CRC, Mask) {
  uint32_t crc = Value("foo", 3);
  ASSERT_NE(crc, Mask(crc));
  ASSERT_NE(crc, Mask(Mask(crc)));
  ASSERT_EQ(crc, Unmask(Mask(crc)));
  ASSERT_EQ(crc, Unmask(Unmask(Mask(Mask(crc)))));
}

TEST_F(CRC, fast_is_on) {
  std::cout << "IsFastCrc32Supported=" << IsFastCrc32Supported() << std::endl;
#if defined(__aarch64__) && defined(__APPLE__)
  EXPECT_TRUE(IsFastCrc32Supported());
#elif defined(__aarch64__) && defined(__linux__)
  if ((getauxval(AT_HWCAP) & HWCAP_CRC32) != 0) {
    EXPECT_TRUE(IsFastCrc32Supported());
  }
#endif
}

TEST_F(CRC, UnalignedAndAllTailLengths) {
  std::vector<char> storage(16 + 512 + 16);
  for (size_t i = 0; i < storage.size(); ++i) {
    storage[i] = static_cast<char>((i * 131 + 17) & 0xff);
  }
  for (size_t align = 0; align < 16; ++align) {
    const char* base = storage.data() + align;
    for (size_t len = 0; len <= 512; ++len) {
      const uint32_t expected = ReferenceExtend(0, base, len);
      ASSERT_EQ(expected, Value(base, len))
          << "align=" << align << " len=" << len;
      if (len >= 2) {
        const size_t split = len / 3;
        const uint32_t part1 = Extend(0, base, split);
        const uint32_t chained = Extend(part1, base + split, len - split);
        ASSERT_EQ(expected, chained)
            << "align=" << align << " len=" << len << " split=" << split;
      }
    }
  }
}

TEST_F(CRC, LargeBuffersAndSeeds) {
  const size_t sizes[] = {
    191, 192, 193,
    383, 384, 385,
    1007, 1008, 1009,
    1024,
    3071, 3072, 3073,
    4096, 16384, 65536
  };
  const uint32_t seeds[] = {0U, 1U, 0x12345678U, 0xdeadbeefU, 0xffffffffU};
  std::vector<char> storage(65536 + 32);
  uint32_t prng = 0x9e3779b9U;
  for (size_t i = 0; i < storage.size(); ++i) {
    prng ^= prng << 13;
    prng ^= prng >> 17;
    prng ^= prng << 5;
    storage[i] = static_cast<char>(prng & 0xff);
  }

  for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
    const size_t len = sizes[s];
    for (size_t align = 0; align < 16; ++align) {
      const char* buf = storage.data() + align;
      for (size_t k = 0; k < sizeof(seeds) / sizeof(seeds[0]); ++k) {
        const uint32_t seed = seeds[k];
        const uint32_t expected = ReferenceExtend(seed, buf, len);
        ASSERT_EQ(expected, Extend(seed, buf, len))
            << "len=" << len << " align=" << align << " seed=" << seed;
      }
    }
  }
}

TEST_F(CRC, Benchmark) {
  const size_t sizes[] = {64, 256, 1024, 4096, 65536};
  std::vector<char> buf(65536, 0x5a);
  for (size_t i = 0; i < buf.size(); ++i) {
    buf[i] = static_cast<char>((i * 73 + 19) & 0xff);
  }
  for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
    const size_t len = sizes[s];
    const size_t total_bytes = 64ULL * 1024 * 1024;
    const size_t iters = total_bytes / len;
    uint32_t crc = 0;
    butil::Timer timer;
    timer.start();
    for (size_t i = 0; i < iters; ++i) {
      crc = Extend(crc, buf.data(), len);
    }
    timer.stop();
    const double elapsed_s = timer.n_elapsed() / 1e9;
    const double mb_per_s = (static_cast<double>(iters * len) / (1024.0 * 1024.0)) / elapsed_s;
    std::cout << "CRC32C size=" << len << "B throughput=" << mb_per_s
              << " MiB/s (crc=" << crc << ")" << std::endl;
  }
}

}  // namespace crc32c
}  // namespace butil
