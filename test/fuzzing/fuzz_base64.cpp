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

#include "butil/base64.h"

#include <stdint.h>

#include <string>

namespace {

const size_t kMaxInputLength = 4096;

void Fail() {
    __builtin_trap();
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > kMaxInputLength) {
        return 0;
    }

    const std::string input(reinterpret_cast<const char*>(data), size);

    // Every binary input must survive an encode/decode round trip. On x86,
    // this exercises the AVX2 path when the host supports it.
    std::string encoded;
    std::string decoded;
    butil::Base64Encode(input, &encoded);
    if (!butil::Base64Decode(encoded, &decoded) || decoded != input) {
        Fail();
    }

    // Arbitrary Base64 text must either decode safely or leave its output
    // untouched, as promised by Base64Decode().
    decoded = "unchanged";
    if (!butil::Base64Decode(input, &decoded) && decoded != "unchanged") {
        Fail();
    }
    return 0;
}
