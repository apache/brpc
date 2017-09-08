// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_DEFAULT_TICK_CLOCK_H_
#define BASE_DEFAULT_TICK_CLOCK_H_

#include "butil/base_export.h"
#include "butil/compiler_specific.h"
#include "butil/time/tick_clock.h"

namespace butil {

// DefaultClock is a Clock implementation that uses TimeTicks::Now().
class BASE_EXPORT DefaultTickClock : public TickClock {
 public:
  virtual ~DefaultTickClock();

  // Simply returns TimeTicks::Now().
  virtual TimeTicks NowTicks() OVERRIDE;
};

}  // namespace butil

#endif  // BASE_DEFAULT_CLOCK_H_
