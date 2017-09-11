// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/files/memory_mapped_file.h"

#include "butil/files/file_path.h"
#include "butil/logging.h"

namespace butil {

MemoryMappedFile::~MemoryMappedFile() {
  CloseHandles();
}

bool MemoryMappedFile::Initialize(const FilePath& file_name) {
  if (IsValid())
    return false;

  file_.Initialize(file_name, File::FLAG_OPEN | File::FLAG_READ);

  if (!file_.IsValid()) {
    DLOG(ERROR) << "Couldn't open " << file_name.AsUTF8Unsafe();
    return false;
  }

  if (!MapFileToMemory()) {
    CloseHandles();
    return false;
  }

  return true;
}

bool MemoryMappedFile::Initialize(File file) {
  if (IsValid())
    return false;

  file_ = file.Pass();

  if (!MapFileToMemory()) {
    CloseHandles();
    return false;
  }

  return true;
}

bool MemoryMappedFile::IsValid() const {
  return data_ != NULL;
}

}  // namespace butil
