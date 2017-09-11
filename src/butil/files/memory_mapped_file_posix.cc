// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/files/memory_mapped_file.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "butil/logging.h"
#include "butil/threading/thread_restrictions.h"

namespace butil {

MemoryMappedFile::MemoryMappedFile() : data_(NULL), length_(0) {
}

bool MemoryMappedFile::MapFileToMemory() {
  ThreadRestrictions::AssertIOAllowed();

  struct stat file_stat;
  if (fstat(file_.GetPlatformFile(), &file_stat) == -1 ) {
    DPLOG(ERROR) << "fstat " << file_.GetPlatformFile();
    return false;
  }
  length_ = file_stat.st_size;

  data_ = static_cast<uint8_t*>(
      mmap(NULL, length_, PROT_READ, MAP_SHARED, file_.GetPlatformFile(), 0));
  if (data_ == MAP_FAILED)
    DPLOG(ERROR) << "mmap " << file_.GetPlatformFile();

  return data_ != MAP_FAILED;
}

void MemoryMappedFile::CloseHandles() {
  ThreadRestrictions::AssertIOAllowed();

  if (data_ != NULL)
    munmap(data_, length_);
  file_.Close();

  data_ = NULL;
  length_ = 0;
}

}  // namespace butil
