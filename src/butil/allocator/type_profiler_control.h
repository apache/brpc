// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_ALLOCATOR_TYPE_PROFILER_CONTROL_H_
#define BASE_ALLOCATOR_TYPE_PROFILER_CONTROL_H_

#include "butil/gtest_prod_util.h"

namespace butil {
namespace type_profiler {

class Controller {
 public:
  static void Stop();
  static bool IsProfiling();

 private:
  FRIEND_TEST_ALL_PREFIXES(TypeProfilerTest,
                           TestProfileNewWithoutProfiledDelete);

  // It must be used only from allowed unit tests.  The following is only
  // allowed for use in unit tests. Profiling should never be restarted in
  // regular use.
  static void Restart();
};

}  // namespace type_profiler
}  // namespace butil

#endif  // BASE_ALLOCATOR_TYPE_PROFILER_CONTROL_H_
