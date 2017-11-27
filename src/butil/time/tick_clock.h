// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUTIL_TICK_CLOCK_H_
#define BUTIL_TICK_CLOCK_H_

#include "butil/base_export.h"
#include "butil/time/time.h"

namespace butil {

// A TickClock is an interface for objects that vend TimeTicks.  It is
// intended to be able to test the behavior of classes with respect to
// non-decreasing time.
//
// See DefaultTickClock (butil/time/default_tick_clock.h) for the default
// implementation that simply uses TimeTicks::Now().
//
// (Other implementations that use TimeTicks::HighResNow() or
// TimeTicks::NowFromSystemTime() should be added as needed.)
//
// See SimpleTestTickClock (butil/test/simple_test_tick_clock.h) for a
// simple test implementation.
//
// See Clock (butil/time/clock.h) for the equivalent interface for Times.
class BUTIL_EXPORT TickClock {
 public:
  virtual ~TickClock();

  // NowTicks() must be safe to call from any thread.  The caller may
  // assume that NowTicks() is monotonic (but not strictly monotonic).
  // In other words, the returned TimeTicks will never decrease with
  // time, although they might "stand still".
  virtual TimeTicks NowTicks() = 0;
};

}  // namespace butil

#endif  // BUTIL_TICK_CLOCK_H_
