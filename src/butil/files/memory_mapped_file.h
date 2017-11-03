// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUTIL_FILES_MEMORY_MAPPED_FILE_H_
#define BUTIL_FILES_MEMORY_MAPPED_FILE_H_

#include "butil/base_export.h"
#include "butil/basictypes.h"
#include "butil/files/file.h"
#include "butil/build_config.h"

#if defined(OS_WIN)
#include <windows.h>
#endif

namespace butil {

class FilePath;

class BUTIL_EXPORT MemoryMappedFile {
 public:
  // The default constructor sets all members to invalid/null values.
  MemoryMappedFile();
  ~MemoryMappedFile();

  // Opens an existing file and maps it into memory. Access is restricted to
  // read only. If this object already points to a valid memory mapped file
  // then this method will fail and return false. If it cannot open the file,
  // the file does not exist, or the memory mapping fails, it will return false.
  // Later we may want to allow the user to specify access.
  bool Initialize(const FilePath& file_name);

  // As above, but works with an already-opened file. MemoryMappedFile takes
  // ownership of |file| and closes it when done.
  bool Initialize(File file);

#if defined(OS_WIN)
  // Opens an existing file and maps it as an image section. Please refer to
  // the Initialize function above for additional information.
  bool InitializeAsImageSection(const FilePath& file_name);
#endif  // OS_WIN

  const uint8_t* data() const { return data_; }
  size_t length() const { return length_; }

  // Is file_ a valid file handle that points to an open, memory mapped file?
  bool IsValid() const;

 private:
  // Map the file to memory, set data_ to that memory address. Return true on
  // success, false on any kind of failure. This is a helper for Initialize().
  bool MapFileToMemory();

  // Closes all open handles.
  void CloseHandles();

  File file_;
  uint8_t* data_;
  size_t length_;

#if defined(OS_WIN)
  win::ScopedHandle file_mapping_;
  bool image_;  // Map as an image.
#endif

  DISALLOW_COPY_AND_ASSIGN(MemoryMappedFile);
};

}  // namespace butil

#endif  // BUTIL_FILES_MEMORY_MAPPED_FILE_H_
