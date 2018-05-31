// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/version.h"

#include <gtest/gtest.h>

namespace {

TEST(VersionTest, DefaultConstructor) {
  Version v;
  EXPECT_FALSE(v.IsValid());
}

TEST(VersionTest, ValueSemantics) {
  Version v1("1.2.3.4");
  EXPECT_TRUE(v1.IsValid());
  Version v3;
  EXPECT_FALSE(v3.IsValid());
  {
    Version v2(v1);
    v3 = v2;
    EXPECT_TRUE(v2.IsValid());
    EXPECT_TRUE(v1.Equals(v2));
  }
  EXPECT_TRUE(v3.Equals(v1));
}

TEST(VersionTest, GetVersionFromString) {
  static const struct version_string {
    const char* input;
    size_t parts;
    bool success;
  } cases[] = {
    {"", 0, false},
    {" ", 0, false},
    {"\t", 0, false},
    {"\n", 0, false},
    {"  ", 0, false},
    {".", 0, false},
    {" . ", 0, false},
    {"0", 1, true},
    {"0.0", 2, true},
    {"65537.0", 0, false},
    {"-1.0", 0, false},
    {"1.-1.0", 0, false},
    {"+1.0", 0, false},
    {"1.+1.0", 0, false},
    {"1.0a", 0, false},
    {"1.2.3.4.5.6.7.8.9.0", 10, true},
    {"02.1", 0, false},
    {"f.1", 0, false},
  };

  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(cases); ++i) {
    Version version(cases[i].input);
    EXPECT_EQ(cases[i].success, version.IsValid());
    if (cases[i].success) {
      EXPECT_EQ(cases[i].parts, version.components().size());
    }
  }
}

TEST(VersionTest, Compare) {
  static const struct version_compare {
    const char* lhs;
    const char* rhs;
    int expected;
  } cases[] = {
    {"1.0", "1.0", 0},
    {"1.0", "0.0", 1},
    {"1.0", "2.0", -1},
    {"1.0", "1.1", -1},
    {"1.1", "1.0", 1},
    {"1.0", "1.0.1", -1},
    {"1.1", "1.0.1", 1},
    {"1.1", "1.0.1", 1},
    {"1.0.0", "1.0", 0},
    {"1.0.3", "1.0.20", -1},
  };
  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(cases); ++i) {
    Version lhs(cases[i].lhs);
    Version rhs(cases[i].rhs);
    EXPECT_EQ(lhs.CompareTo(rhs), cases[i].expected) <<
        cases[i].lhs << " ? " << cases[i].rhs;

    EXPECT_EQ(lhs.IsOlderThan(cases[i].rhs), (cases[i].expected == -1));
  }
}

TEST(VersionTest, CompareToWildcardString) {
  static const struct version_compare {
    const char* lhs;
    const char* rhs;
    int expected;
  } cases[] = {
    {"1.0", "1.*", 0},
    {"1.0", "0.*", 1},
    {"1.0", "2.*", -1},
    {"1.2.3", "1.2.3.*", 0},
    {"10.0", "1.0.*", 1},
    {"1.0", "3.0.*", -1},
    {"1.4", "1.3.0.*", 1},
    {"1.3.9", "1.3.*", 0},
    {"1.4.1", "1.3.*", 1},
    {"1.3", "1.4.5.*", -1},
    {"1.5", "1.4.5.*", 1},
    {"1.3.9", "1.3.*", 0},
    {"1.2.0.0.0.0", "1.2.*", 0},
  };
  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(cases); ++i) {
    const Version version(cases[i].lhs);
    const int result = version.CompareToWildcardString(cases[i].rhs);
    EXPECT_EQ(result, cases[i].expected) << cases[i].lhs << "?" << cases[i].rhs;
  }
}

TEST(VersionTest, IsValidWildcardString) {
  static const struct version_compare {
    const char* version;
    bool expected;
  } cases[] = {
    {"1.0", true},
    {"", false},
    {"1.2.3.4.5.6", true},
    {"1.2.3.*", true},
    {"1.2.3.5*", false},
    {"1.2.3.56*", false},
    {"1.*.3", false},
    {"20.*", true},
    {"+2.*", false},
    {"*", false},
    {"*.2", false},
  };
  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(cases); ++i) {
    EXPECT_EQ(Version::IsValidWildcardString(cases[i].version),
        cases[i].expected) << cases[i].version << "?" << cases[i].expected;
  }
}

}  // namespace
