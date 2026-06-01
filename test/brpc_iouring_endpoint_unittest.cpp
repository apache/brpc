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
#include <gflags/gflags.h>
#if BRPC_WITH_IOURING

#include "brpc/iouring/iouring_endpoint.h"
#include "brpc/iouring/iouring_block_pool.h"

namespace brpc {
namespace iouring {

// ===========================================================================
// IouringReadSlot – struct layout tests
// ===========================================================================

TEST(IouringReadSlotTest, default_constructed) {
    IouringReadSlot slot;
    EXPECT_EQ(slot.buf, nullptr);
    EXPECT_EQ(slot.buf_index, 0);
    EXPECT_EQ(slot.size, 0u);
}

TEST(IouringReadSlotTest, field_assignment) {
    IouringReadSlot slot;
    char dummy_buf[8192];
    slot.buf = dummy_buf;
    slot.buf_index = 42;
    slot.size = 8192;

    EXPECT_EQ(slot.buf, static_cast<void*>(dummy_buf));
    EXPECT_EQ(slot.buf_index, 42);
    EXPECT_EQ(slot.size, 8192u);
}

TEST(IouringReadSlotTest, copy_constructible) {
    IouringReadSlot orig;
    char buf[4096];
    orig.buf = buf;
    orig.buf_index = 10;
    orig.size = 4096;

    IouringReadSlot copy(orig);  // should be copyable for lambda capture
    EXPECT_EQ(copy.buf, static_cast<void*>(buf));
    EXPECT_EQ(copy.buf_index, 10);
    EXPECT_EQ(copy.size, 4096u);

    // Original should be unchanged.
    EXPECT_EQ(orig.buf, static_cast<void*>(buf));
}

// ===========================================================================
// SidOp – operation type enum
// ===========================================================================

TEST(SidOpTest, op_type_values) {
    // Verify that SidOp::OpType has exactly ADD and REMOVE after refactoring.
    // (RELEASE_SLOT was removed when IouringReadSlotPool was eliminated.)
    EXPECT_EQ(static_cast<int>(SidOp::ADD), 0);
    EXPECT_EQ(static_cast<int>(SidOp::REMOVE), 1);
    EXPECT_LE(static_cast<int>(SidOp::REMOVE), 1)
        << "SidOp should only have ADD and REMOVE after refactoring";
}

TEST(SidOpTest, default_is_add) {
    SidOp op;
    EXPECT_EQ(op.type, SidOp::ADD);
    EXPECT_EQ(op.sid, 0u);
    // read_slot should be zero-initialized by default constructor.
    EXPECT_EQ(op.read_slot.buf, nullptr);
}

TEST(SidOpTest, remove_with_read_slot) {
    char buf[8192];
    IouringReadSlot slot;
    slot.buf = buf;
    slot.buf_index = 5;
    slot.size = 8192;

    SocketId sid(12345);
    SidOp op(sid, SidOp::REMOVE, slot);

    EXPECT_EQ(op.type, SidOp::REMOVE);
    EXPECT_EQ(op.sid, 12345u);
    EXPECT_EQ(op.read_slot.buf, static_cast<void*>(buf));
    EXPECT_EQ(op.read_slot.buf_index, 5);
    EXPECT_EQ(op.read_slot.size, 8192u);
}

TEST(SidOpTest, add_without_read_slot) {
    SidOp op(SocketId(99), SidOp::ADD);
    EXPECT_EQ(op.type, SidOp::ADD);
    EXPECT_EQ(op.sid, 99u);
    EXPECT_EQ(op.read_slot.buf, nullptr);
}

// ===========================================================================
// IsFixedBuffersEnabled / GetWriteBufIndex helpers
// ===========================================================================

TEST(IouringHelperTest, is_fixed_buffers_enabled_default_false) {
    // Default gflag value is false; test without init.
    // Note: FLAGS_iouring_register_buffers defaults to false.
    EXPECT_FALSE(IsFixedBuffersEnabled());
}

TEST(IouringHelperTest, get_write_buf_index_when_disabled) {
    // When fixed buffers are disabled, GetWriteBufIndex should return -1
    // because no regions are registered.
    FLAGS_iouring_register_buffers = false;

    struct io_uring ring;
    char stack_buf[1024];

    int idx = GetWriteBufIndex(&ring, stack_buf);
    EXPECT_EQ(idx, -1);
}

}  // namespace iouring
}  // namespace brpc

#endif  // BRPC_WITH_IOURING
