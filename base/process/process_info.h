// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_PROCESS_PROCESS_PROCESS_INFO_H_
#define BASE_PROCESS_PROCESS_PROCESS_INFO_H_

#include "base/base_export.h"
#include "base/basictypes.h"

namespace base {

class Time;

// Vends information about the current process.
class BASE_EXPORT CurrentProcessInfo {
 public:
  // Returns the time at which the process was launched. May be empty if an
  // error occurred retrieving the information.
  static const Time CreationTime();
};

}  // namespace base

#endif  // BASE_PROCESS_PROCESS_PROCESS_INFO_H_
