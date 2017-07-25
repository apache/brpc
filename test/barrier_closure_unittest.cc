// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/barrier_closure.h"

#include "base/bind.h"
#include <gtest/gtest.h>

namespace {

void Increment(int* count) { (*count)++; }

TEST(BarrierClosureTest, RunImmediatelyForZeroClosures) {
  int count = 0;
  base::Closure doneClosure(base::Bind(&Increment, base::Unretained(&count)));

  base::Closure barrierClosure = base::BarrierClosure(0, doneClosure);
  EXPECT_EQ(1, count);
}

TEST(BarrierClosureTest, RunAfterNumClosures) {
  int count = 0;
  base::Closure doneClosure(base::Bind(&Increment, base::Unretained(&count)));

  base::Closure barrierClosure = base::BarrierClosure(2, doneClosure);
  EXPECT_EQ(0, count);

  barrierClosure.Run();
  EXPECT_EQ(0, count);

  barrierClosure.Run();
  EXPECT_EQ(1, count);
}

}  // namespace
