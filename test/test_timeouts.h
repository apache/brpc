// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TEST_TEST_TIMEOUTS_H_
#define BASE_TEST_TEST_TIMEOUTS_H_

#include "butil/basictypes.h"
#include "butil/logging.h"
#include "butil/time/time.h"

// Returns common timeouts to use in tests. Makes it possible to adjust
// the timeouts for different environments (like Valgrind).
class TestTimeouts {
 public:
  // Initializes the timeouts. Non thread-safe. Should be called exactly once
  // by the test suite.
  static void Initialize();

  // Timeout for actions that are expected to finish "almost instantly".
  static butil::TimeDelta tiny_timeout() {
    DCHECK(initialized_);
    return butil::TimeDelta::FromMilliseconds(tiny_timeout_ms_);
  }

  // Timeout to wait for something to happen. If you are not sure
  // which timeout to use, this is the one you want.
  static butil::TimeDelta action_timeout() {
    DCHECK(initialized_);
    return butil::TimeDelta::FromMilliseconds(action_timeout_ms_);
  }

  // Timeout longer than the above, but still suitable to use
  // multiple times in a single test. Use if the timeout above
  // is not sufficient.
  static butil::TimeDelta action_max_timeout() {
    DCHECK(initialized_);
    return butil::TimeDelta::FromMilliseconds(action_max_timeout_ms_);
  }

  // Timeout for a large test that may take a few minutes to run.
  static butil::TimeDelta large_test_timeout() {
    DCHECK(initialized_);
    return butil::TimeDelta::FromMilliseconds(large_test_timeout_ms_);
  }

  // Timeout for a single test launched used built-in test launcher.
  // Do not use outside of the test launcher.
  static butil::TimeDelta test_launcher_timeout() {
    DCHECK(initialized_);
    return butil::TimeDelta::FromMilliseconds(test_launcher_timeout_ms_);
  }

 private:
  static bool initialized_;

  static int tiny_timeout_ms_;
  static int action_timeout_ms_;
  static int action_max_timeout_ms_;
  static int large_test_timeout_ms_;
  static int test_launcher_timeout_ms_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(TestTimeouts);
};

#endif  // BASE_TEST_TEST_TIMEOUTS_H_
