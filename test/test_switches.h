// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUTIL_TEST_TEST_SWITCHES_H_
#define BUTIL_TEST_TEST_SWITCHES_H_

namespace switches {

// All switches in alphabetical order. The switches should be documented
// alongside the definition of their values in the .cc file.
extern const char kTestLargeTimeout[];
extern const char kTestLauncherBatchLimit[];
extern const char kTestLauncherBotMode[];
extern const char kTestLauncherDebugLauncher[];
extern const char kTestLauncherFilterFile[];
extern const char kTestLauncherJobs[];
extern const char kTestLauncherOutput[];
extern const char kTestLauncherRetryLimit[];
extern const char kTestLauncherSummaryOutput[];
extern const char kTestLauncherPrintTestStdio[];
extern const char kTestLauncherShardIndex[];
extern const char kTestLauncherTotalShards[];
extern const char kTestLauncherTimeout[];
extern const char kTestTinyTimeout[];
extern const char kUiTestActionTimeout[];
extern const char kUiTestActionMaxTimeout[];

}  // namespace switches

#endif  // BUTIL_TEST_TEST_SWITCHES_H_
