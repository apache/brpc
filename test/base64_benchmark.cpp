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

#include "butil/third_party/modp_b64/modp_b64.h"

#include <stdint.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

volatile size_t g_sink;

template <typename F>
double Benchmark(size_t iterations, F f) {
    const std::chrono::steady_clock::time_point start =
        std::chrono::steady_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        g_sink += f();
    }
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();
}

}  // namespace

int main() {
    const size_t sizes[] = {
        1, 2, 3, 8, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384,
        512, 768, 1024, 1536, 2048, 4096, 8192, 16384, 32768, 65536,
    };
    std::printf("bytes,encode_MiB_s,decode_MiB_s\n");
    for (size_t size : sizes) {
        std::string input(size, '\0');
        for (size_t i = 0; i < size; ++i) {
            input[i] = static_cast<char>((i * 37 + size) & 0xff);
        }
        std::vector<char> encoded(modp_b64_encode_len(size));
        const size_t encoded_size =
            modp_b64_encode(encoded.data(), input.data(), input.size());
        std::vector<char> decoded(modp_b64_decode_len(encoded_size));
        if (modp_b64_decode(decoded.data(), encoded.data(), encoded_size) != size ||
            std::memcmp(decoded.data(), input.data(), size) != 0) {
            return 2;
        }

        const size_t iterations = std::max<size_t>(
            10000, std::min<size_t>(
                3000000, (64U << 20) / std::max<size_t>(size, 1)));
        const double encode_seconds = Benchmark(iterations, [&] {
            return modp_b64_encode(encoded.data(), input.data(), input.size());
        });
        const double decode_seconds = Benchmark(iterations, [&] {
            return modp_b64_decode(decoded.data(), encoded.data(), encoded_size);
        });
        const double mib = static_cast<double>(size) * iterations / (1U << 20);
        std::printf("%zu,%.1f,%.1f\n", size, mib / encode_seconds,
                    mib / decode_seconds);
    }
    return g_sink == 0 ? 1 : 0;
}
