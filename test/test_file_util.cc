// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/test_file_util.h"

#include "test/test_timeouts.h"
#include "base/threading/platform_thread.h"

namespace base {

bool EvictFileFromSystemCacheWithRetry(const FilePath& path) {
  const int kCycles = 10;
  const TimeDelta kDelay = TestTimeouts::action_timeout() / kCycles;
  for (int i = 0; i < kCycles; i++) {
    if (EvictFileFromSystemCache(path))
      return true;
    PlatformThread::Sleep(kDelay);
  }
  return false;
}

}  // namespace base
