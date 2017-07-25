// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests of CancellationFlag class.

#include "base/synchronization/cancellation_flag.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/synchronization/spin_wait.h"
#include "base/time/time.h"
#include <gtest/gtest.h>

namespace base {

namespace {

//------------------------------------------------------------------------------
// Define our test class.
//------------------------------------------------------------------------------

void CancelHelper(CancellationFlag* flag) {
#if GTEST_HAS_DEATH_TEST
  ASSERT_DEBUG_DEATH(flag->Set(), "");
#endif
}

TEST(CancellationFlagTest, SimpleSingleThreadedTest) {
  CancellationFlag flag;
  ASSERT_FALSE(flag.IsSet());
  flag.Set();
  ASSERT_TRUE(flag.IsSet());
}

TEST(CancellationFlagTest, DoubleSetTest) {
  CancellationFlag flag;
  ASSERT_FALSE(flag.IsSet());
  flag.Set();
  ASSERT_TRUE(flag.IsSet());
  flag.Set();
  ASSERT_TRUE(flag.IsSet());
}

}  // namespace

}  // namespace base
