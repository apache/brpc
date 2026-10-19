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

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "bthread/context.h"

#if defined(BTHREAD_CONTEXT_PLATFORM_linux_x86_64)

namespace {

constexpr size_t kStackSize = 8192;
constexpr size_t kXmmRegisterCount = 10;

struct XmmState {
    uint8_t registers[kXmmRegisterCount][16];
};

bthread_fcontext_t g_main_context;
bthread_fcontext_t g_test_context;
XmmState g_clobber_state;

extern "C" intptr_t jump_with_xmm_state(
    bthread_fcontext_t* old_context, bthread_fcontext_t new_context,
    intptr_t value, const XmmState* input, XmmState* output);

// Keep the register setup, context switch, and register capture in one
// assembly function so the compiler cannot use an XMM register between them.
__asm__(
".text\n"
".p2align 4,,15\n"
".type jump_with_xmm_state,@function\n"
"jump_with_xmm_state:\n"
"    movups 0x00(%rcx), %xmm6\n"
"    movups 0x10(%rcx), %xmm7\n"
"    movups 0x20(%rcx), %xmm8\n"
"    movups 0x30(%rcx), %xmm9\n"
"    movups 0x40(%rcx), %xmm10\n"
"    movups 0x50(%rcx), %xmm11\n"
"    movups 0x60(%rcx), %xmm12\n"
"    movups 0x70(%rcx), %xmm13\n"
"    movups 0x80(%rcx), %xmm14\n"
"    movups 0x90(%rcx), %xmm15\n"
"    pushq %r8\n"
"    xorl %ecx, %ecx\n"
"    call bthread_jump_fcontext\n"
"    popq %r8\n"
"    movups %xmm6, 0x00(%r8)\n"
"    movups %xmm7, 0x10(%r8)\n"
"    movups %xmm8, 0x20(%r8)\n"
"    movups %xmm9, 0x30(%r8)\n"
"    movups %xmm10, 0x40(%r8)\n"
"    movups %xmm11, 0x50(%r8)\n"
"    movups %xmm12, 0x60(%r8)\n"
"    movups %xmm13, 0x70(%r8)\n"
"    movups %xmm14, 0x80(%r8)\n"
"    movups %xmm15, 0x90(%r8)\n"
"    ret\n"
".size jump_with_xmm_state,.-jump_with_xmm_state\n");

void overwrite_xmm_and_return(intptr_t) {
    XmmState ignored = {};
    jump_with_xmm_state(&g_test_context, g_main_context, 0,
                        &g_clobber_state, &ignored);
}

TEST(BthreadContextTest, preserves_xmm6_through_xmm15) {
    XmmState expected;
    XmmState actual = {};
    for (size_t reg = 0; reg < kXmmRegisterCount; ++reg) {
        for (size_t byte = 0; byte < 16; ++byte) {
            expected.registers[reg][byte] =
                static_cast<uint8_t>(reg * 16 + byte);
            g_clobber_state.registers[reg][byte] =
                static_cast<uint8_t>(0xff - reg * 16 - byte);
        }
    }

    void* stack = std::malloc(kStackSize);
    ASSERT_NE(nullptr, stack);
    g_main_context = nullptr;
    g_test_context = bthread_make_fcontext(
        static_cast<char*>(stack) + kStackSize, kStackSize,
        overwrite_xmm_and_return);

    jump_with_xmm_state(&g_main_context, g_test_context, 0, &expected, &actual);

    EXPECT_EQ(0, std::memcmp(&expected, &actual, sizeof(expected)));
    std::free(stack);
}

}  // namespace

#endif  // BTHREAD_CONTEXT_PLATFORM_linux_x86_64
