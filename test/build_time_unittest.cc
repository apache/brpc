// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/build_time.h"

#include <gtest/gtest.h>

TEST(BuildTime, DateLooksValid) {
  char build_date[] = __DATE__;

  EXPECT_EQ(11u, strlen(build_date));
  EXPECT_EQ(' ', build_date[3]);
  EXPECT_EQ(' ', build_date[6]);
}

TEST(BuildTime, TimeLooksValid) {
  char build_time[] = __TIME__;

  EXPECT_EQ(8u, strlen(build_time));
  EXPECT_EQ(':', build_time[2]);
  EXPECT_EQ(':', build_time[5]);
}

TEST(BuildTime, DoesntCrash) {
  // Since __DATE__ isn't updated unless one does a clobber build, we can't
  // really test the value returned by it, except to check that it doesn't
  // crash.
  base::GetBuildTime();
}
