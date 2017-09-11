// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/path_service.h"

#include "butil/basictypes.h"
#include "butil/file_util.h"
#include "butil/files/file_path.h"
#include "butil/files/scoped_temp_dir.h"
#include "butil/strings/string_util.h"
#include "butil/build_config.h"
#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>
#include <gtest/gtest.h>

#if defined(OS_WIN)
#include <userenv.h>
#include "butil/win/windows_version.h"
// userenv.dll is required for GetDefaultUserProfileDirectory().
#pragma comment(lib, "userenv.lib")
#endif

namespace {

// Returns true if PathService::Get returns true and sets the path parameter
// to non-empty for the given PathService::DirType enumeration value.
bool ALLOW_UNUSED ReturnsValidPath(int dir_type) {
  butil::FilePath path;
  bool result = PathService::Get(dir_type, &path);

  // Some paths might not exist on some platforms in which case confirming
  // |result| is true and !path.empty() is the best we can do.
  bool check_path_exists = true;
#if defined(OS_POSIX)
  // If chromium has never been started on this account, the cache path may not
  // exist.
  if (dir_type == butil::DIR_CACHE)
    check_path_exists = false;
#endif
#if defined(OS_LINUX)
  // On the linux try-bots: a path is returned (e.g. /home/chrome-bot/Desktop),
  // but it doesn't exist.
  if (dir_type == butil::DIR_USER_DESKTOP)
    check_path_exists = false;
#endif
#if defined(OS_WIN)
  if (dir_type == butil::DIR_DEFAULT_USER_QUICK_LAUNCH) {
    // On Windows XP, the Quick Launch folder for the "Default User" doesn't
    // exist by default. At least confirm that the path returned begins with the
    // Default User's profile path.
    if (butil::win::GetVersion() < butil::win::VERSION_VISTA) {
      wchar_t default_profile_path[MAX_PATH];
      DWORD size = arraysize(default_profile_path);
      return (result &&
              ::GetDefaultUserProfileDirectory(default_profile_path, &size) &&
              StartsWith(path.value(), default_profile_path, false));
    }
  } else if (dir_type == butil::DIR_TASKBAR_PINS) {
    // There is no pinned-to-taskbar shortcuts prior to Win7.
    if (butil::win::GetVersion() < butil::win::VERSION_WIN7)
      check_path_exists = false;
  }
#endif
#if defined(OS_MACOSX)
  if (dir_type != butil::DIR_EXE && dir_type != butil::DIR_MODULE &&
      dir_type != butil::FILE_EXE && dir_type != butil::FILE_MODULE) {
    if (path.ReferencesParent())
      return false;
  }
#else
  if (path.ReferencesParent())
    return false;
#endif
  return result && !path.empty() && (!check_path_exists ||
                                     butil::PathExists(path));
}

#if defined(OS_WIN)
// Function to test any directory keys that are not supported on some versions
// of Windows. Checks that the function fails and that the returned path is
// empty.
bool ReturnsInvalidPath(int dir_type) {
  butil::FilePath path;
  bool result = PathService::Get(dir_type, &path);
  return !result && path.empty();
}
#endif

}  // namespace

// On the Mac this winds up using some autoreleased objects, so we need to
// be a testing::Test.
typedef testing::Test PathServiceTest;

// FIXME(gejun): Fail due to
// [0710/142222:FATAL:path_service.cc(212)] Check failed: path.empty(). provider should not have modified path
#if defined(FixedPathServiceTestGet)
// Test that all PathService::Get calls return a value and a true result
// in the development environment.  (This test was created because a few
// later changes to Get broke the semantics of the function and yielded the
// correct value while returning false.)
TEST_F(PathServiceTest, Get) {
  for (int key = butil::PATH_START + 1; key < butil::PATH_END; ++key) {
#if defined(OS_ANDROID)
    if (key == butil::FILE_MODULE || key == butil::DIR_USER_DESKTOP ||
        key == butil::DIR_HOME)
      continue;  // Android doesn't implement these.
#elif defined(OS_IOS)
    if (key == butil::DIR_USER_DESKTOP)
      continue;  // iOS doesn't implement DIR_USER_DESKTOP;
#endif
    EXPECT_PRED1(ReturnsValidPath, key);
  }
#if defined(OS_WIN)
  for (int key = butil::PATH_WIN_START + 1; key < butil::PATH_WIN_END; ++key) {
    bool valid = true;
    switch(key) {
      case butil::DIR_LOCAL_APP_DATA_LOW:
        // DIR_LOCAL_APP_DATA_LOW is not supported prior Vista and is expected
        // to fail.
        valid = butil::win::GetVersion() >= butil::win::VERSION_VISTA;
        break;
      case butil::DIR_APP_SHORTCUTS:
        // DIR_APP_SHORTCUTS is not supported prior Windows 8 and is expected to
        // fail.
        valid = butil::win::GetVersion() >= butil::win::VERSION_WIN8;
        break;
    }

    if (valid)
      EXPECT_TRUE(ReturnsValidPath(key)) << key;
    else
      EXPECT_TRUE(ReturnsInvalidPath(key)) << key;
  }
#elif defined(OS_MACOSX)
  for (int key = butil::PATH_MAC_START + 1; key < butil::PATH_MAC_END; ++key) {
    EXPECT_PRED1(ReturnsValidPath, key);
  }
#elif defined(OS_ANDROID)
  for (int key = butil::PATH_ANDROID_START + 1; key < butil::PATH_ANDROID_END;
       ++key) {
    EXPECT_PRED1(ReturnsValidPath, key);
  }
#elif defined(OS_POSIX)
  for (int key = butil::PATH_POSIX_START + 1; key < butil::PATH_POSIX_END;
       ++key) {
    EXPECT_PRED1(ReturnsValidPath, key);
  }
#endif
}
#endif

// Test that all versions of the Override function of PathService do what they
// are supposed to do.
TEST_F(PathServiceTest, Override) {
  int my_special_key = 666;
  butil::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  butil::FilePath fake_cache_dir(temp_dir.path().AppendASCII("cache"));
  // PathService::Override should always create the path provided if it doesn't
  // exist.
  EXPECT_TRUE(PathService::Override(my_special_key, fake_cache_dir));
  EXPECT_TRUE(butil::PathExists(fake_cache_dir));

  butil::FilePath fake_cache_dir2(temp_dir.path().AppendASCII("cache2"));
  // PathService::OverrideAndCreateIfNeeded should obey the |create| parameter.
  PathService::OverrideAndCreateIfNeeded(my_special_key,
                                         fake_cache_dir2,
                                         false,
                                         false);
  EXPECT_FALSE(butil::PathExists(fake_cache_dir2));
  EXPECT_TRUE(PathService::OverrideAndCreateIfNeeded(my_special_key,
                                                     fake_cache_dir2,
                                                     false,
                                                     true));
  EXPECT_TRUE(butil::PathExists(fake_cache_dir2));

#if defined(OS_POSIX)
  butil::FilePath non_existent(
      butil::MakeAbsoluteFilePath(temp_dir.path()).AppendASCII("non_existent"));
  EXPECT_TRUE(non_existent.IsAbsolute());
  EXPECT_FALSE(butil::PathExists(non_existent));
#if !defined(OS_ANDROID)
  // This fails because MakeAbsoluteFilePath fails for non-existent files.
  // Earlier versions of Bionic libc don't fail for non-existent files, so
  // skip this check on Android.
  EXPECT_FALSE(PathService::OverrideAndCreateIfNeeded(my_special_key,
                                                      non_existent,
                                                      false,
                                                      false));
#endif
  // This works because indicating that |non_existent| is absolute skips the
  // internal MakeAbsoluteFilePath call.
  EXPECT_TRUE(PathService::OverrideAndCreateIfNeeded(my_special_key,
                                                     non_existent,
                                                     true,
                                                     false));
  // Check that the path has been overridden and no directory was created.
  EXPECT_FALSE(butil::PathExists(non_existent));
  butil::FilePath path;
  EXPECT_TRUE(PathService::Get(my_special_key, &path));
  EXPECT_EQ(non_existent, path);
#endif
}

// Check if multiple overrides can co-exist.
TEST_F(PathServiceTest, OverrideMultiple) {
  int my_special_key = 666;
  butil::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  butil::FilePath fake_cache_dir1(temp_dir.path().AppendASCII("1"));
  EXPECT_TRUE(PathService::Override(my_special_key, fake_cache_dir1));
  EXPECT_TRUE(butil::PathExists(fake_cache_dir1));
  ASSERT_EQ(1, butil::WriteFile(fake_cache_dir1.AppendASCII("t1"), ".", 1));

  butil::FilePath fake_cache_dir2(temp_dir.path().AppendASCII("2"));
  EXPECT_TRUE(PathService::Override(my_special_key + 1, fake_cache_dir2));
  EXPECT_TRUE(butil::PathExists(fake_cache_dir2));
  ASSERT_EQ(1, butil::WriteFile(fake_cache_dir2.AppendASCII("t2"), ".", 1));

  butil::FilePath result;
  EXPECT_TRUE(PathService::Get(my_special_key, &result));
  // Override might have changed the path representation but our test file
  // should be still there.
  EXPECT_TRUE(butil::PathExists(result.AppendASCII("t1")));
  EXPECT_TRUE(PathService::Get(my_special_key + 1, &result));
  EXPECT_TRUE(butil::PathExists(result.AppendASCII("t2")));
}

TEST_F(PathServiceTest, RemoveOverride) {
  // Before we start the test we have to call RemoveOverride at least once to
  // clear any overrides that might have been left from other tests.
  PathService::RemoveOverride(butil::DIR_TEMP);

  butil::FilePath original_user_data_dir;
  EXPECT_TRUE(PathService::Get(butil::DIR_TEMP, &original_user_data_dir));
  EXPECT_FALSE(PathService::RemoveOverride(butil::DIR_TEMP));

  butil::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  EXPECT_TRUE(PathService::Override(butil::DIR_TEMP, temp_dir.path()));
  butil::FilePath new_user_data_dir;
  EXPECT_TRUE(PathService::Get(butil::DIR_TEMP, &new_user_data_dir));
  EXPECT_NE(original_user_data_dir, new_user_data_dir);

  EXPECT_TRUE(PathService::RemoveOverride(butil::DIR_TEMP));
  EXPECT_TRUE(PathService::Get(butil::DIR_TEMP, &new_user_data_dir));
  EXPECT_EQ(original_user_data_dir, new_user_data_dir);
}
