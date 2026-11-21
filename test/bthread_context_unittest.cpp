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

#include <cfenv>
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

#if defined(BTHREAD_CONTEXT_PLATFORM_ppc64_elfv2)

namespace {

constexpr size_t kStackSize = 64 * 1024;
// Non-volatile floating-point state of the ELFv2 ABI: f14-f31 and v20-v31.
// The layout is what jump_with_fp_state loads and stores.
constexpr size_t kFprCount = 18;
constexpr size_t kVrCount = 12;

struct alignas(16) FpState {
    double fprs[kFprCount];           // f14-f31 at offsets 0..143
    uint8_t vrs[kVrCount][16];        // v20-v31 at offsets 144..335
};

bthread_fcontext_t g_main_context;
bthread_fcontext_t g_test_context;
FpState g_clobber_state;
bool g_context_saw_its_state;
int g_context_entry_rounding_mode;
int g_context_rounding_mode;
uintptr_t g_stack_begin;
uintptr_t g_stack_end;
bool g_stack_walk_terminated;

extern "C" intptr_t jump_with_fp_state(
    bthread_fcontext_t* old_context, bthread_fcontext_t new_context,
    intptr_t value, const FpState* input, FpState* output);

// Load the registers from *input, switch to new_context and store them into
// *output once this context is resumed, all in one assembly function so the
// compiler cannot use any of them in between. The output pointer lives in
// r31 across the switch, which exercises the GPR part of the frame too. The
// registers are callee-saved, so the caller's values are kept in the frame
// and put back before returning.
__asm__(
".text\n"
".align 2\n"
".machine push\n"
".machine altivec\n"
".type jump_with_fp_state, @function\n"
"jump_with_fp_state:\n"
"    addis  %r2, %r12, .TOC.-jump_with_fp_state@ha\n"
"    addi   %r2, %r2, .TOC.-jump_with_fp_state@l\n"
"    .localentry jump_with_fp_state, . - jump_with_fp_state\n"
"    mflr  %r0\n"
"    std  %r0, 16(%r1)\n"
"    stdu  %r1, -384(%r1)\n"
"    std  %r2, 24(%r1)\n"
"    std  %r31, 32(%r1)\n"
"    stfd  %f14, 48(%r1)\n"
"    stfd  %f15, 56(%r1)\n"
"    stfd  %f16, 64(%r1)\n"
"    stfd  %f17, 72(%r1)\n"
"    stfd  %f18, 80(%r1)\n"
"    stfd  %f19, 88(%r1)\n"
"    stfd  %f20, 96(%r1)\n"
"    stfd  %f21, 104(%r1)\n"
"    stfd  %f22, 112(%r1)\n"
"    stfd  %f23, 120(%r1)\n"
"    stfd  %f24, 128(%r1)\n"
"    stfd  %f25, 136(%r1)\n"
"    stfd  %f26, 144(%r1)\n"
"    stfd  %f27, 152(%r1)\n"
"    stfd  %f28, 160(%r1)\n"
"    stfd  %f29, 168(%r1)\n"
"    stfd  %f30, 176(%r1)\n"
"    stfd  %f31, 184(%r1)\n"
"    li  %r11, 192\n"
"    stvx  %v20, %r1, %r11\n"
"    li  %r11, 208\n"
"    stvx  %v21, %r1, %r11\n"
"    li  %r11, 224\n"
"    stvx  %v22, %r1, %r11\n"
"    li  %r11, 240\n"
"    stvx  %v23, %r1, %r11\n"
"    li  %r11, 256\n"
"    stvx  %v24, %r1, %r11\n"
"    li  %r11, 272\n"
"    stvx  %v25, %r1, %r11\n"
"    li  %r11, 288\n"
"    stvx  %v26, %r1, %r11\n"
"    li  %r11, 304\n"
"    stvx  %v27, %r1, %r11\n"
"    li  %r11, 320\n"
"    stvx  %v28, %r1, %r11\n"
"    li  %r11, 336\n"
"    stvx  %v29, %r1, %r11\n"
"    li  %r11, 352\n"
"    stvx  %v30, %r1, %r11\n"
"    li  %r11, 368\n"
"    stvx  %v31, %r1, %r11\n"
"    mr  %r31, %r7\n"
"    lfd  %f14, 0(%r6)\n"
"    lfd  %f15, 8(%r6)\n"
"    lfd  %f16, 16(%r6)\n"
"    lfd  %f17, 24(%r6)\n"
"    lfd  %f18, 32(%r6)\n"
"    lfd  %f19, 40(%r6)\n"
"    lfd  %f20, 48(%r6)\n"
"    lfd  %f21, 56(%r6)\n"
"    lfd  %f22, 64(%r6)\n"
"    lfd  %f23, 72(%r6)\n"
"    lfd  %f24, 80(%r6)\n"
"    lfd  %f25, 88(%r6)\n"
"    lfd  %f26, 96(%r6)\n"
"    lfd  %f27, 104(%r6)\n"
"    lfd  %f28, 112(%r6)\n"
"    lfd  %f29, 120(%r6)\n"
"    lfd  %f30, 128(%r6)\n"
"    lfd  %f31, 136(%r6)\n"
"    li  %r11, 144\n"
"    lvx  %v20, %r6, %r11\n"
"    li  %r11, 160\n"
"    lvx  %v21, %r6, %r11\n"
"    li  %r11, 176\n"
"    lvx  %v22, %r6, %r11\n"
"    li  %r11, 192\n"
"    lvx  %v23, %r6, %r11\n"
"    li  %r11, 208\n"
"    lvx  %v24, %r6, %r11\n"
"    li  %r11, 224\n"
"    lvx  %v25, %r6, %r11\n"
"    li  %r11, 240\n"
"    lvx  %v26, %r6, %r11\n"
"    li  %r11, 256\n"
"    lvx  %v27, %r6, %r11\n"
"    li  %r11, 272\n"
"    lvx  %v28, %r6, %r11\n"
"    li  %r11, 288\n"
"    lvx  %v29, %r6, %r11\n"
"    li  %r11, 304\n"
"    lvx  %v30, %r6, %r11\n"
"    li  %r11, 320\n"
"    lvx  %v31, %r6, %r11\n"
"    # preserve_fpu = false\n"
"    li  %r6, 0\n"
"    bl  bthread_jump_fcontext\n"
"    nop\n"
"    stfd  %f14, 0(%r31)\n"
"    stfd  %f15, 8(%r31)\n"
"    stfd  %f16, 16(%r31)\n"
"    stfd  %f17, 24(%r31)\n"
"    stfd  %f18, 32(%r31)\n"
"    stfd  %f19, 40(%r31)\n"
"    stfd  %f20, 48(%r31)\n"
"    stfd  %f21, 56(%r31)\n"
"    stfd  %f22, 64(%r31)\n"
"    stfd  %f23, 72(%r31)\n"
"    stfd  %f24, 80(%r31)\n"
"    stfd  %f25, 88(%r31)\n"
"    stfd  %f26, 96(%r31)\n"
"    stfd  %f27, 104(%r31)\n"
"    stfd  %f28, 112(%r31)\n"
"    stfd  %f29, 120(%r31)\n"
"    stfd  %f30, 128(%r31)\n"
"    stfd  %f31, 136(%r31)\n"
"    li  %r11, 144\n"
"    stvx  %v20, %r31, %r11\n"
"    li  %r11, 160\n"
"    stvx  %v21, %r31, %r11\n"
"    li  %r11, 176\n"
"    stvx  %v22, %r31, %r11\n"
"    li  %r11, 192\n"
"    stvx  %v23, %r31, %r11\n"
"    li  %r11, 208\n"
"    stvx  %v24, %r31, %r11\n"
"    li  %r11, 224\n"
"    stvx  %v25, %r31, %r11\n"
"    li  %r11, 240\n"
"    stvx  %v26, %r31, %r11\n"
"    li  %r11, 256\n"
"    stvx  %v27, %r31, %r11\n"
"    li  %r11, 272\n"
"    stvx  %v28, %r31, %r11\n"
"    li  %r11, 288\n"
"    stvx  %v29, %r31, %r11\n"
"    li  %r11, 304\n"
"    stvx  %v30, %r31, %r11\n"
"    li  %r11, 320\n"
"    stvx  %v31, %r31, %r11\n"
"    lfd  %f14, 48(%r1)\n"
"    lfd  %f15, 56(%r1)\n"
"    lfd  %f16, 64(%r1)\n"
"    lfd  %f17, 72(%r1)\n"
"    lfd  %f18, 80(%r1)\n"
"    lfd  %f19, 88(%r1)\n"
"    lfd  %f20, 96(%r1)\n"
"    lfd  %f21, 104(%r1)\n"
"    lfd  %f22, 112(%r1)\n"
"    lfd  %f23, 120(%r1)\n"
"    lfd  %f24, 128(%r1)\n"
"    lfd  %f25, 136(%r1)\n"
"    lfd  %f26, 144(%r1)\n"
"    lfd  %f27, 152(%r1)\n"
"    lfd  %f28, 160(%r1)\n"
"    lfd  %f29, 168(%r1)\n"
"    lfd  %f30, 176(%r1)\n"
"    lfd  %f31, 184(%r1)\n"
"    li  %r11, 192\n"
"    lvx  %v20, %r1, %r11\n"
"    li  %r11, 208\n"
"    lvx  %v21, %r1, %r11\n"
"    li  %r11, 224\n"
"    lvx  %v22, %r1, %r11\n"
"    li  %r11, 240\n"
"    lvx  %v23, %r1, %r11\n"
"    li  %r11, 256\n"
"    lvx  %v24, %r1, %r11\n"
"    li  %r11, 272\n"
"    lvx  %v25, %r1, %r11\n"
"    li  %r11, 288\n"
"    lvx  %v26, %r1, %r11\n"
"    li  %r11, 304\n"
"    lvx  %v27, %r1, %r11\n"
"    li  %r11, 320\n"
"    lvx  %v28, %r1, %r11\n"
"    li  %r11, 336\n"
"    lvx  %v29, %r1, %r11\n"
"    li  %r11, 352\n"
"    lvx  %v30, %r1, %r11\n"
"    li  %r11, 368\n"
"    lvx  %v31, %r1, %r11\n"
"    ld  %r31, 32(%r1)\n"
"    addi  %r1, %r1, 384\n"
"    ld  %r0, 16(%r1)\n"
"    mtlr  %r0\n"
"    blr\n"
".machine pop\n"
".size jump_with_fp_state, .-jump_with_fp_state\n");

void clobber_fp_state_and_return(intptr_t) {
    FpState seen = {};
    // Load the clobber pattern and switch back. When the main context
    // resumes this one, the registers must hold the clobber pattern again.
    jump_with_fp_state(&g_test_context, g_main_context, 0, &g_clobber_state,
                       &seen);
    g_context_saw_its_state =
        std::memcmp(&seen, &g_clobber_state, sizeof(seen)) == 0;
    bthread_jump_fcontext(&g_test_context, g_main_context, 0);
}

TEST(BthreadContextTest, preserves_nonvolatile_fprs_and_vrs) {
    FpState expected = {};
    FpState actual = {};
    for (size_t reg = 0; reg < kFprCount; ++reg) {
        expected.fprs[reg] = 1000.5 + reg;
        g_clobber_state.fprs[reg] = -2000.25 - reg;
    }
    for (size_t reg = 0; reg < kVrCount; ++reg) {
        for (size_t byte = 0; byte < 16; ++byte) {
            expected.vrs[reg][byte] = static_cast<uint8_t>(reg * 16 + byte);
            g_clobber_state.vrs[reg][byte] =
                static_cast<uint8_t>(0xff - reg * 16 - byte);
        }
    }

    void* stack = std::malloc(kStackSize);
    ASSERT_NE(nullptr, stack);
    g_main_context = nullptr;
    g_context_saw_its_state = false;
    g_test_context = bthread_make_fcontext(
        static_cast<char*>(stack) + kStackSize, kStackSize,
        clobber_fp_state_and_return);

    // The context clobbers every register and switches back.
    jump_with_fp_state(&g_main_context, g_test_context, 0, &expected, &actual);
    EXPECT_EQ(0, std::memcmp(&expected, &actual, sizeof(expected)));

    // Resume the context so that it can check its own registers.
    std::memset(&actual, 0, sizeof(actual));
    jump_with_fp_state(&g_main_context, g_test_context, 0, &expected, &actual);
    EXPECT_EQ(0, std::memcmp(&expected, &actual, sizeof(expected)));
    EXPECT_TRUE(g_context_saw_its_state);
    std::free(stack);
}

void change_rounding_mode_and_return(intptr_t) {
    g_context_entry_rounding_mode = std::fegetround();
    std::fesetround(FE_DOWNWARD);
    bthread_jump_fcontext(&g_test_context, g_main_context, 0);
    // Resumed after the main context ran with another rounding mode.
    g_context_rounding_mode = std::fegetround();
    bthread_jump_fcontext(&g_test_context, g_main_context, 0);
}

TEST(BthreadContextTest, preserves_fpscr_rounding_mode) {
    const int saved_rounding_mode = std::fegetround();
    void* stack = std::malloc(kStackSize);
    ASSERT_NE(nullptr, stack);
    g_main_context = nullptr;
    g_context_entry_rounding_mode = -1;
    g_context_rounding_mode = -1;

    ASSERT_EQ(0, std::fesetround(FE_UPWARD));
    g_test_context = bthread_make_fcontext(
        static_cast<char*>(stack) + kStackSize, kStackSize,
        change_rounding_mode_and_return);
    bthread_jump_fcontext(&g_main_context, g_test_context, 0);
    // A new context starts with the FPSCR of the thread that made it, and
    // the FE_DOWNWARD set inside the context must not leak out of it.
    EXPECT_EQ(FE_UPWARD, g_context_entry_rounding_mode);
    EXPECT_EQ(FE_UPWARD, std::fegetround());

    bthread_jump_fcontext(&g_main_context, g_test_context, 0);
    EXPECT_EQ(FE_DOWNWARD, g_context_rounding_mode);
    EXPECT_EQ(FE_UPWARD, std::fegetround());

    std::fesetround(saved_rounding_mode);
    std::free(stack);
}

void walk_back_chain_and_return(intptr_t) {
    // Follow the back chains from our frame. The walk must end on a zero
    // word inside the context's stack, without ever leaving the stack.
    void* const* frame =
        static_cast<void* const*>(__builtin_frame_address(0));
    int steps = 0;
    while (frame != nullptr &&
           reinterpret_cast<uintptr_t>(frame) >= g_stack_begin &&
           reinterpret_cast<uintptr_t>(frame) < g_stack_end && ++steps < 16) {
        frame = static_cast<void* const*>(frame[0]);
    }
    g_stack_walk_terminated = frame == nullptr;
    bthread_jump_fcontext(&g_test_context, g_main_context, 0);
}

TEST(BthreadContextTest, stack_walk_of_new_context_terminates) {
    void* stack = std::malloc(kStackSize);
    ASSERT_NE(nullptr, stack);
    // bthread reuses stacks, so the memory holds stale data.
    std::memset(stack, 0xa5, kStackSize);
    g_stack_begin = reinterpret_cast<uintptr_t>(stack);
    g_stack_end = g_stack_begin + kStackSize;
    g_stack_walk_terminated = false;
    g_main_context = nullptr;
    g_test_context = bthread_make_fcontext(
        static_cast<char*>(stack) + kStackSize, kStackSize,
        walk_back_chain_and_return);
    bthread_jump_fcontext(&g_main_context, g_test_context, 0);
    EXPECT_TRUE(g_stack_walk_terminated);
    std::free(stack);
}

}  // namespace

#endif  // BTHREAD_CONTEXT_PLATFORM_ppc64_elfv2
