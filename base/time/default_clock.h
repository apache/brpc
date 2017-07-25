// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_DEFAULT_CLOCK_H_
#define BASE_DEFAULT_CLOCK_H_

#include "base/base_export.h"
#include "base/compiler_specific.h"
#include "base/time/clock.h"

namespace base {

// DefaultClock is a Clock implementation that uses Time::Now().
class BASE_EXPORT DefaultClock : public Clock {
 public:
  virtual ~DefaultClock();

  // Simply returns Time::Now().
  virtual Time Now() OVERRIDE;
};

}  // namespace base

#endif  // BASE_DEFAULT_CLOCK_H_
