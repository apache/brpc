// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/file_util.h"

#import <Foundation/Foundation.h>
#include <copyfile.h>

#include "butil/basictypes.h"
#include "butil/files/file_path.h"
#include "butil/mac/foundation_util.h"
#include "butil/strings/string_util.h"
#include "butil/threading/thread_restrictions.h"

namespace butil {
namespace internal {

bool CopyFileUnsafe(const FilePath& from_path, const FilePath& to_path) {
  ThreadRestrictions::AssertIOAllowed();
  return (copyfile(from_path.value().c_str(),
                   to_path.value().c_str(), NULL, COPYFILE_DATA) == 0);
}

}  // namespace internal

bool GetTempDir(butil::FilePath* path) {
  NSString* tmp = NSTemporaryDirectory();
  if (tmp == nil)
    return false;
  *path = butil::mac::NSStringToFilePath(tmp);
  return true;
}

FilePath GetHomeDir() {
  NSString* tmp = NSHomeDirectory();
  if (tmp != nil) {
    FilePath mac_home_dir = butil::mac::NSStringToFilePath(tmp);
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

}  // namespace butil
