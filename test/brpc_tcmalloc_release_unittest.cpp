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

#include <gtest/gtest.h>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <unistd.h>

// Weakly reference the C symbol so the test runs even without tcmalloc.
extern "C" void MallocExtension_ReleaseFreeMemory(void) __attribute__((weak));

namespace {

class AlarmGuard {
public:
    explicit AlarmGuard(unsigned seconds) {
        std::signal(SIGALRM, &AlarmHandler);
        alarm(seconds);
    }
    ~AlarmGuard() { alarm(0); }
private:
    static void AlarmHandler(int) {
        // Fail fast on potential hang.
        std::fprintf(stderr, "Timed out in ReleaseFreeMemory test\n");
        _exit(124);
    }
};

static void churn_allocations(std::size_t bytes_total) {
    const std::size_t chunk = 1 << 20; // 1 MiB
    std::vector<void*> ptrs;
    ptrs.reserve(bytes_total / chunk);
    for (std::size_t i = 0; i < bytes_total; i += chunk) {
        void* p = std::malloc(chunk);
        if (!p) break;
        std::memset(p, 0xA5, chunk);
        ptrs.push_back(p);
    }
    for (void* p : ptrs) std::free(p);
}

TEST(TCMallocReleaseTest, NoHangAfterRelease) {
    AlarmGuard guard(10);
    churn_allocations(128ULL << 20); // 128 MiB

    if (MallocExtension_ReleaseFreeMemory) {
        MallocExtension_ReleaseFreeMemory();
    }

    churn_allocations(128ULL << 20); // 128 MiB
    SUCCEED();
}

} // namespace


