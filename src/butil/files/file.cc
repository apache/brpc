// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/files/file.h"
#include "butil/files/file_path.h"

namespace butil {

File::Info::Info()
    : size(0),
      is_directory(false),
      is_symbolic_link(false) {
}

File::Info::~Info() {
}

File::File()
    : error_details_(FILE_ERROR_FAILED),
      created_(false),
      async_(false) {
}

#if !defined(OS_NACL)
File::File(const FilePath& name, uint32_t flags)
    : error_details_(FILE_OK),
      created_(false),
      async_(false) {
  Initialize(name, flags);
}
#endif

File::File(PlatformFile platform_file)
    : file_(platform_file),
      error_details_(FILE_OK),
      created_(false),
      async_(false) {
#if defined(OS_POSIX)
  DCHECK_GE(platform_file, -1);
#endif
}

File::File(Error error_details)
    : error_details_(error_details),
      created_(false),
      async_(false) {
}

File::File(RValue other)
    : file_(other.object->TakePlatformFile()),
      error_details_(other.object->error_details()),
      created_(other.object->created()),
      async_(other.object->async_) {
}

File::~File() {
  // Go through the AssertIOAllowed logic.
  Close();
}

File& File::operator=(RValue other) {
  if (this != other.object) {
    Close();
    SetPlatformFile(other.object->TakePlatformFile());
    error_details_ = other.object->error_details();
    created_ = other.object->created();
    async_ = other.object->async_;
  }
  return *this;
}

#if !defined(OS_NACL)
void File::Initialize(const FilePath& name, uint32_t flags) {
  if (name.ReferencesParent()) {
    error_details_ = FILE_ERROR_ACCESS_DENIED;
    return;
  }
  InitializeUnsafe(name, flags);
}
#endif

std::string File::ErrorToString(Error error) {
  switch (error) {
    case FILE_OK:
      return "FILE_OK";
    case FILE_ERROR_FAILED:
      return "FILE_ERROR_FAILED";
    case FILE_ERROR_IN_USE:
      return "FILE_ERROR_IN_USE";
    case FILE_ERROR_EXISTS:
      return "FILE_ERROR_EXISTS";
    case FILE_ERROR_NOT_FOUND:
      return "FILE_ERROR_NOT_FOUND";
    case FILE_ERROR_ACCESS_DENIED:
      return "FILE_ERROR_ACCESS_DENIED";
    case FILE_ERROR_TOO_MANY_OPENED:
      return "FILE_ERROR_TOO_MANY_OPENED";
    case FILE_ERROR_NO_MEMORY:
      return "FILE_ERROR_NO_MEMORY";
    case FILE_ERROR_NO_SPACE:
      return "FILE_ERROR_NO_SPACE";
    case FILE_ERROR_NOT_A_DIRECTORY:
      return "FILE_ERROR_NOT_A_DIRECTORY";
    case FILE_ERROR_INVALID_OPERATION:
      return "FILE_ERROR_INVALID_OPERATION";
    case FILE_ERROR_SECURITY:
      return "FILE_ERROR_SECURITY";
    case FILE_ERROR_ABORT:
      return "FILE_ERROR_ABORT";
    case FILE_ERROR_NOT_A_FILE:
      return "FILE_ERROR_NOT_A_FILE";
    case FILE_ERROR_NOT_EMPTY:
      return "FILE_ERROR_NOT_EMPTY";
    case FILE_ERROR_INVALID_URL:
      return "FILE_ERROR_INVALID_URL";
    case FILE_ERROR_IO:
      return "FILE_ERROR_IO";
    case FILE_ERROR_MAX:
      break;
  }

  NOTREACHED();
  return "";
}

}  // namespace butil
