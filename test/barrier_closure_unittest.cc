// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/barrier_closure.h"

#include "butil/bind.h"
#include <gtest/gtest.h>

namespace {

void Increment(int* count) { (*count)++; }

TEST(BarrierClosureTest, RunImmediatelyForZeroClosures) {
  int count = 0;
  butil::Closure doneClosure(butil::Bind(&Increment, butil::Unretained(&count)));

  butil::Closure barrierClosure = butil::BarrierClosure(0, doneClosure);
  EXPECT_EQ(1, count);
}

TEST(BarrierClosureTest, RunAfterNumClosures) {
  int count = 0;
  butil::Closure doneClosure(butil::Bind(&Increment, butil::Unretained(&count)));

  butil::Closure barrierClosure = butil::BarrierClosure(2, doneClosure);
  EXPECT_EQ(0, count);

  barrierClosure.Run();
  EXPECT_EQ(0, count);

  barrierClosure.Run();
  EXPECT_EQ(1, count);
}

}  // namespace
