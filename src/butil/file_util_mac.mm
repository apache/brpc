// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/file_util.h"

#import <Foundation/Foundation.h>
#include <copyfile.h>

#include "base/basictypes.h"
#include "base/files/file_path.h"
#include "base/mac/foundation_util.h"
#include "base/strings/string_util.h"
#include "base/threading/thread_restrictions.h"

namespace base {
namespace internal {

bool CopyFileUnsafe(const FilePath& from_path, const FilePath& to_path) {
  ThreadRestrictions::AssertIOAllowed();
  return (copyfile(from_path.value().c_str(),
                   to_path.value().c_str(), NULL, COPYFILE_DATA) == 0);
}

}  // namespace internal

bool GetTempDir(base::FilePath* path) {
  NSString* tmp = NSTemporaryDirectory();
  if (tmp == nil)
    return false;
  *path = base::mac::NSStringToFilePath(tmp);
  return true;
}

FilePath GetHomeDir() {
  NSString* tmp = NSHomeDirectory();
  if (tmp != nil) {
    FilePath mac_home_dir = base::mac::NSStringToFilePath(tmp);
    if (!mac_home_dir.empty())
      return mac_home_dir;
  }

  // Fall back on temp dir if no home directory is defined.
  FilePath rv;
  if (GetTempDir(&rv))
    return rv;

  // Last resort.
  return FilePath("/tmp");
}

}  // namespace base
