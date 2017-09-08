// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_PROCESS_PROCESS_PROCESS_INFO_H_
#define BASE_PROCESS_PROCESS_PROCESS_INFO_H_

#include "butil/base_export.h"
#include "butil/basictypes.h"

namespace butil {

class Time;

// Vends information about the current process.
class BASE_EXPORT CurrentProcessInfo {
 public:
  // Returns the time at which the process was launched. May be empty if an
  // error occurred retrieving the information.
  static const Time CreationTime();
};

}  // namespace butil

#endif  // BASE_PROCESS_PROCESS_PROCESS_INFO_H_
