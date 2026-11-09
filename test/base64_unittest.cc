// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/base64.h"

#include <gtest/gtest.h>

namespace butil {

TEST(Base64Test, Basic) {
  const std::string kText = "hello world";
  const std::string kBase64Text = "aGVsbG8gd29ybGQ=";

  std::string encoded;
  std::string decoded;
  bool ok;

  Base64Encode(kText, &encoded);
  EXPECT_EQ(kBase64Text, encoded);

  ok = Base64Decode(encoded, &decoded);
  EXPECT_TRUE(ok);
  EXPECT_EQ(kText, decoded);
}

TEST(Base64Test, RoundTripBinaryPayloads) {
  for (size_t size = 0; size <= 257; ++size) {
    std::string input;
    input.reserve(size);
    for (size_t i = 0; i < size; ++i) {
      input.push_back(static_cast<char>((i * 37 + size) & 0xff));
    }

    std::string encoded;
    std::string decoded;
    Base64Encode(input, &encoded);
    ASSERT_TRUE(Base64Decode(encoded, &decoded)) << "size=" << size;
    EXPECT_EQ(input, decoded) << "size=" << size;
  }
}

TEST(Base64Test, RejectsInvalidInput) {
  std::string decoded;
  EXPECT_FALSE(Base64Decode("abc", &decoded));
  EXPECT_FALSE(Base64Decode("abcd!", &decoded));
  EXPECT_FALSE(Base64Decode("abcd====", &decoded));
  EXPECT_FALSE(Base64Decode("AAAA$AAA", &decoded));
}

TEST(Base64Test, RejectsLongInvalidInput) {
  const std::string valid(1024, 'A');
  const char kInvalidChars[] = {'!', '=', static_cast<char>(0x80)};
  for (char invalid : kInvalidChars) {
    std::string input = valid;
    input[input.size() / 2] = invalid;
    std::string decoded;
    EXPECT_FALSE(Base64Decode(input, &decoded))
        << "invalid byte=" << static_cast<unsigned char>(invalid);
  }
}

TEST(Base64Test, DecodesLongInputWithPadding) {
  const std::string input(193, 'x');
  std::string encoded;
  std::string decoded;
  Base64Encode(input, &encoded);
  ASSERT_EQ("==", encoded.substr(encoded.size() - 2));
  ASSERT_TRUE(Base64Decode(encoded, &decoded));
  EXPECT_EQ(input, decoded);
}

}  // namespace butil
