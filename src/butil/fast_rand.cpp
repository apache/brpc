// Copyright (c) 2015 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Author: Ge,Jun (gejun@baidu.com)
// Date: Thu Dec 31 13:35:39 CST 2015

#include <limits>          // numeric_limits
#include <math.h>
#include "butil/basictypes.h"
#include "butil/macros.h"
#include "butil/time.h"     // gettimeofday_us()
#include "butil/fast_rand.h"

namespace butil {

// This state can be seeded with any value.
typedef uint64_t SplitMix64Seed;

// A very fast generator passing BigCrush. Due to its 64-bit state, it's
// solely for seeding xorshift128+.
inline uint64_t splitmix64_next(SplitMix64Seed* seed) {
    uint64_t z = (*seed += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

// xorshift128+ is the fastest generator passing BigCrush without systematic
// failures
inline uint64_t xorshift128_next(FastRandSeed* seed) {
    uint64_t s1 = seed->s[0];
    const uint64_t s0 = seed->s[1];
    seed->s[0] = s0;
    s1 ^= s1 << 23; // a
    seed->s[1] = s1 ^ s0 ^ (s1 >> 18) ^ (s0 >> 5); // b, c
    return seed->s[1] + s0;
}

// seed xorshift128+ with splitmix64.
void init_fast_rand_seed(FastRandSeed* seed) {
    SplitMix64Seed seed4seed = butil::gettimeofday_us();
    seed->s[0] = splitmix64_next(&seed4seed);
    seed->s[1] = splitmix64_next(&seed4seed);
}

inline uint64_t fast_rand_impl(uint64_t range, FastRandSeed* seed) {
    // Separating uint64_t values into following intervals:
    //   [0,range-1][range,range*2-1] ... [uint64_max/range*range,uint64_max]
    // If the generated 64-bit random value falls into any interval except the
    // last one, the probability of taking any value inside [0, range-1] is
    // same. If the value falls into last interval, we retry the process until
    // the value falls into other intervals. If min/max are limited to 32-bits,
    // the retrying is rare. The amortized retrying count at maximum is 1 when 
    // range equals 2^32. A corner case is that even if the range is power of
    // 2(e.g. min=0 max=65535) in which case the retrying can be avoided, we
    // still retry currently. The reason is just to keep the code simpler
    // and faster for most cases.
    const uint64_t div = std::numeric_limits<uint64_t>::max() / range;
    uint64_t result;
    do {
        result = xorshift128_next(seed) / div;
    } while (result >= range);
    return result;
}

// Seeds for different threads are stored separately in thread-local storage.
static __thread FastRandSeed _tls_seed = { { 0, 0 } };

// True if the seed is (probably) uninitialized. There's definitely false
// positive, but it's OK for us.
inline bool need_init(const FastRandSeed& seed) {
    return seed.s[0] == 0 && seed.s[1] == 0;
}

uint64_t fast_rand() {
    if (need_init(_tls_seed)) {
        init_fast_rand_seed(&_tls_seed);
    }
    return xorshift128_next(&_tls_seed);
}

uint64_t fast_rand(FastRandSeed* seed) {
    return xorshift128_next(seed);
}

uint64_t fast_rand_less_than(uint64_t range) {
    if (range == 0) {
        return 0;
    }
    if (need_init(_tls_seed)) {
        init_fast_rand_seed(&_tls_seed);
    }
    return fast_rand_impl(range, &_tls_seed);
}

int64_t fast_rand_in_64(int64_t min, int64_t max) {
    if (need_init(_tls_seed)) {
        init_fast_rand_seed(&_tls_seed);
    }
    if (min >= max) {
        if (min == max) {
            return min;
        }
        const int64_t tmp = min;
        min = max;
        max = tmp;
    }
    int64_t range = max - min + 1;
    if (range == 0) {
        // max = INT64_MAX, min = INT64_MIN
        return (int64_t)xorshift128_next(&_tls_seed);
    }
    return min + (int64_t)fast_rand_impl(max - min + 1, &_tls_seed);
}

uint64_t fast_rand_in_u64(uint64_t min, uint64_t max) {
    if (need_init(_tls_seed)) {
        init_fast_rand_seed(&_tls_seed);
    }
    if (min >= max) {
        if (min == max) {
            return min;
        }
        const uint64_t tmp = min;
        min = max;
        max = tmp;
    }
    uint64_t range = max - min + 1;
    if (range == 0) {
        // max = UINT64_MAX, min = UINT64_MIN
        return xorshift128_next(&_tls_seed);
    }
    return min + fast_rand_impl(range, &_tls_seed);
}

inline double fast_rand_double(FastRandSeed* seed) {
    // Copied from rand_util.cc
    COMPILE_ASSERT(std::numeric_limits<double>::radix == 2, otherwise_use_scalbn);
    static const int kBits = std::numeric_limits<double>::digits;
    uint64_t random_bits = xorshift128_next(seed) & ((UINT64_C(1) << kBits) - 1);
    double result = ldexp(static_cast<double>(random_bits), -1 * kBits);
    return result;
}

double fast_rand_double() {
    if (need_init(_tls_seed)) {
        init_fast_rand_seed(&_tls_seed);
    }
    return fast_rand_double(&_tls_seed);
}

void fast_rand_bytes(void* output, size_t output_length) {
    const size_t n = output_length / 8;
    for (size_t i = 0; i < n; ++i) {
        static_cast<uint64_t*>(output)[i] = fast_rand();
    }
    const size_t m = output_length - n * 8;
    if (m) {
        uint8_t* p = static_cast<uint8_t*>(output) + n * 8;
        uint64_t r = fast_rand();
        for (size_t i = 0; i < m; ++i) {
            p[i] = (r & 0xFF);
            r = (r >> 8);
        }
    }
}

}  // namespace butil
