// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/big_endian.h"

#include "butil/strings/string_piece.h"
#include <gtest/gtest.h>

namespace butil {

TEST(BigEndianReaderTest, ReadsValues) {
  char data[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xA, 0xB, 0xC };
  char buf[2];
  uint8_t u8;
  uint16_t u16;
  uint32_t u32;
  butil::StringPiece piece;
  BigEndianReader reader(data, sizeof(data));

  EXPECT_TRUE(reader.Skip(2));
  EXPECT_EQ(data + 2, reader.ptr());
  EXPECT_EQ(reader.remaining(), static_cast<int>(sizeof(data)) - 2);
  EXPECT_TRUE(reader.ReadBytes(buf, sizeof(buf)));
  EXPECT_EQ(0x2, buf[0]);
  EXPECT_EQ(0x3, buf[1]);
  EXPECT_TRUE(reader.ReadU8(&u8));
  EXPECT_EQ(0x4, u8);
  EXPECT_TRUE(reader.ReadU16(&u16));
  EXPECT_EQ(0x0506, u16);
  EXPECT_TRUE(reader.ReadU32(&u32));
  EXPECT_EQ(0x0708090Au, u32);
  butil::StringPiece expected(reader.ptr(), 2);
  EXPECT_TRUE(reader.ReadPiece(&piece, 2));
  EXPECT_EQ(2u, piece.size());
  EXPECT_EQ(expected.data(), piece.data());
}

TEST(BigEndianReaderTest, RespectsLength) {
  char data[4];
  char buf[2];
  uint8_t u8;
  uint16_t u16;
  uint32_t u32;
  butil::StringPiece piece;
  BigEndianReader reader(data, sizeof(data));
  // 4 left
  EXPECT_FALSE(reader.Skip(6));
  EXPECT_TRUE(reader.Skip(1));
  // 3 left
  EXPECT_FALSE(reader.ReadU32(&u32));
  EXPECT_FALSE(reader.ReadPiece(&piece, 4));
  EXPECT_TRUE(reader.Skip(2));
  // 1 left
  EXPECT_FALSE(reader.ReadU16(&u16));
  EXPECT_FALSE(reader.ReadBytes(buf, 2));
  EXPECT_TRUE(reader.Skip(1));
  // 0 left
  EXPECT_FALSE(reader.ReadU8(&u8));
  EXPECT_EQ(0, reader.remaining());
}

TEST(BigEndianWriterTest, WritesValues) {
  char expected[] = { 0, 0, 2, 3, 4, 5, 6, 7, 8, 9, 0xA };
  char data[sizeof(expected)];
  char buf[] = { 0x2, 0x3 };
  memset(data, 0, sizeof(data));
  BigEndianWriter writer(data, sizeof(data));

  EXPECT_TRUE(writer.Skip(2));
  EXPECT_TRUE(writer.WriteBytes(buf, sizeof(buf)));
  EXPECT_TRUE(writer.WriteU8(0x4));
  EXPECT_TRUE(writer.WriteU16(0x0506));
  EXPECT_TRUE(writer.WriteU32(0x0708090A));
  EXPECT_EQ(0, memcmp(expected, data, sizeof(expected)));
}

TEST(BigEndianWriterTest, RespectsLength) {
  char data[4];
  char buf[2];
  uint8_t u8 = 0;
  uint16_t u16 = 0;
  uint32_t u32 = 0;
  BigEndianWriter writer(data, sizeof(data));
  // 4 left
  EXPECT_FALSE(writer.Skip(6));
  EXPECT_TRUE(writer.Skip(1));
  // 3 left
  EXPECT_FALSE(writer.WriteU32(u32));
  EXPECT_TRUE(writer.Skip(2));
  // 1 left
  EXPECT_FALSE(writer.WriteU16(u16));
  EXPECT_FALSE(writer.WriteBytes(buf, 2));
  EXPECT_TRUE(writer.Skip(1));
  // 0 left
  EXPECT_FALSE(writer.WriteU8(u8));
  EXPECT_EQ(0, writer.remaining());
}

}  // namespace butil
