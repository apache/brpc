// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/environment.h"
#include "butil/file_util.h"
#include "butil/sys_info.h"
#include "butil/threading/platform_thread.h"
#include "butil/time/time.h"
#include <gtest/gtest.h>
#include <gtest/gtest.h>

typedef testing::Test SysInfoTest;
using butil::FilePath;

#if defined(OS_POSIX) && !defined(OS_MACOSX) && !defined(OS_ANDROID)
TEST_F(SysInfoTest, MaxSharedMemorySize) {
  // We aren't actually testing that it's correct, just that it's sane.
  EXPECT_GT(butil::SysInfo::MaxSharedMemorySize(), 0u);
}
#endif

TEST_F(SysInfoTest, NumProcs) {
  // We aren't actually testing that it's correct, just that it's sane.
  EXPECT_GE(butil::SysInfo::NumberOfProcessors(), 1);
}

TEST_F(SysInfoTest, AmountOfMem) {
  // We aren't actually testing that it's correct, just that it's sane.
  EXPECT_GT(butil::SysInfo::AmountOfPhysicalMemory(), 0);
  EXPECT_GT(butil::SysInfo::AmountOfPhysicalMemoryMB(), 0);
  // The maxmimal amount of virtual memory can be zero which means unlimited.
  EXPECT_GE(butil::SysInfo::AmountOfVirtualMemory(), 0);
}

TEST_F(SysInfoTest, AmountOfFreeDiskSpace) {
  // We aren't actually testing that it's correct, just that it's sane.
  FilePath tmp_path;
  ASSERT_TRUE(butil::GetTempDir(&tmp_path));
  EXPECT_GT(butil::SysInfo::AmountOfFreeDiskSpace(tmp_path), 0)
            << tmp_path.value();
}

#if defined(OS_WIN) || defined(OS_MACOSX)
TEST_F(SysInfoTest, OperatingSystemVersionNumbers) {
  int32_t os_major_version = -1;
  int32_t os_minor_version = -1;
  int32_t os_bugfix_version = -1;
  butil::SysInfo::OperatingSystemVersionNumbers(&os_major_version,
                                               &os_minor_version,
                                               &os_bugfix_version);
  EXPECT_GT(os_major_version, -1);
  EXPECT_GT(os_minor_version, -1);
  EXPECT_GT(os_bugfix_version, -1);
}
#endif

TEST_F(SysInfoTest, Uptime) {
  int64_t up_time_1 = butil::SysInfo::Uptime();
  // UpTime() is implemented internally using TimeTicks::Now(), which documents
  // system resolution as being 1-15ms. Sleep a little longer than that.
  butil::PlatformThread::Sleep(butil::TimeDelta::FromMilliseconds(20));
  int64_t up_time_2 = butil::SysInfo::Uptime();
  EXPECT_GT(up_time_1, 0);
  EXPECT_GT(up_time_2, up_time_1);
}

#if defined(OS_CHROMEOS)

TEST_F(SysInfoTest, GoogleChromeOSVersionNumbers) {
  int32_t os_major_version = -1;
  int32_t os_minor_version = -1;
  int32_t os_bugfix_version = -1;
  const char* kLsbRelease =
      "FOO=1234123.34.5\n"
      "CHROMEOS_RELEASE_VERSION=1.2.3.4\n";
  butil::SysInfo::SetChromeOSVersionInfoForTest(kLsbRelease, butil::Time());
  butil::SysInfo::OperatingSystemVersionNumbers(&os_major_version,
                                               &os_minor_version,
                                               &os_bugfix_version);
  EXPECT_EQ(1, os_major_version);
  EXPECT_EQ(2, os_minor_version);
  EXPECT_EQ(3, os_bugfix_version);
}

TEST_F(SysInfoTest, GoogleChromeOSVersionNumbersFirst) {
  int32_t os_major_version = -1;
  int32_t os_minor_version = -1;
  int32_t os_bugfix_version = -1;
  const char* kLsbRelease =
      "CHROMEOS_RELEASE_VERSION=1.2.3.4\n"
      "FOO=1234123.34.5\n";
  butil::SysInfo::SetChromeOSVersionInfoForTest(kLsbRelease, butil::Time());
  butil::SysInfo::OperatingSystemVersionNumbers(&os_major_version,
                                               &os_minor_version,
                                               &os_bugfix_version);
  EXPECT_EQ(1, os_major_version);
  EXPECT_EQ(2, os_minor_version);
  EXPECT_EQ(3, os_bugfix_version);
}

TEST_F(SysInfoTest, GoogleChromeOSNoVersionNumbers) {
  int32_t os_major_version = -1;
  int32_t os_minor_version = -1;
  int32_t os_bugfix_version = -1;
  const char* kLsbRelease = "FOO=1234123.34.5\n";
  butil::SysInfo::SetChromeOSVersionInfoForTest(kLsbRelease, butil::Time());
  butil::SysInfo::OperatingSystemVersionNumbers(&os_major_version,
                                               &os_minor_version,
                                               &os_bugfix_version);
  EXPECT_EQ(0, os_major_version);
  EXPECT_EQ(0, os_minor_version);
  EXPECT_EQ(0, os_bugfix_version);
}

TEST_F(SysInfoTest, GoogleChromeOSLsbReleaseTime) {
  const char* kLsbRelease = "CHROMEOS_RELEASE_VERSION=1.2.3.4";
  // Use a fake time that can be safely displayed as a string.
  const butil::Time lsb_release_time(butil::Time::FromDoubleT(12345.6));
  butil::SysInfo::SetChromeOSVersionInfoForTest(kLsbRelease, lsb_release_time);
  butil::Time parsed_lsb_release_time = butil::SysInfo::GetLsbReleaseTime();
  EXPECT_DOUBLE_EQ(lsb_release_time.ToDoubleT(),
                   parsed_lsb_release_time.ToDoubleT());
}

TEST_F(SysInfoTest, IsRunningOnChromeOS) {
  butil::SysInfo::SetChromeOSVersionInfoForTest("", butil::Time());
  EXPECT_FALSE(butil::SysInfo::IsRunningOnChromeOS());

  const char* kLsbRelease1 =
      "CHROMEOS_RELEASE_NAME=Non Chrome OS\n"
      "CHROMEOS_RELEASE_VERSION=1.2.3.4\n";
  butil::SysInfo::SetChromeOSVersionInfoForTest(kLsbRelease1, butil::Time());
  EXPECT_FALSE(butil::SysInfo::IsRunningOnChromeOS());

  const char* kLsbRelease2 =
      "CHROMEOS_RELEASE_NAME=Chrome OS\n"
      "CHROMEOS_RELEASE_VERSION=1.2.3.4\n";
  butil::SysInfo::SetChromeOSVersionInfoForTest(kLsbRelease2, butil::Time());
  EXPECT_TRUE(butil::SysInfo::IsRunningOnChromeOS());

  const char* kLsbRelease3 =
      "CHROMEOS_RELEASE_NAME=Chromium OS\n";
  butil::SysInfo::SetChromeOSVersionInfoForTest(kLsbRelease3, butil::Time());
  EXPECT_TRUE(butil::SysInfo::IsRunningOnChromeOS());
}

#endif  // OS_CHROMEOS
