// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUTIL_FILES_FILE_H_
#define BUTIL_FILES_FILE_H_

#include "butil/build_config.h"
#if defined(OS_WIN)
#include <windows.h>
#endif

#if defined(OS_POSIX)
#include <sys/stat.h>
#endif

#include <string>

#include "butil/base_export.h"
#include "butil/basictypes.h"
#include "butil/files/scoped_file.h"
#include "butil/move.h"
#include "butil/time/time.h"

#if defined(OS_WIN)
#include "butil/win/scoped_handle.h"
#endif

namespace butil {

class FilePath;

#if defined(OS_WIN)
typedef HANDLE PlatformFile;
#elif defined(OS_POSIX)
typedef int PlatformFile;

#if defined(OS_BSD) || defined(OS_MACOSX) || defined(OS_NACL)
typedef struct stat stat_wrapper_t;
#else
typedef struct stat64 stat_wrapper_t;
#endif
#endif  // defined(OS_POSIX)

// Thin wrapper around an OS-level file.
// Note that this class does not provide any support for asynchronous IO, other
// than the ability to create asynchronous handles on Windows.
//
// Note about const: this class does not attempt to determine if the underlying
// file system object is affected by a particular method in order to consider
// that method const or not. Only methods that deal with member variables in an
// obvious non-modifying way are marked as const. Any method that forward calls
// to the OS is not considered const, even if there is no apparent change to
// member variables.
class BUTIL_EXPORT File {
  MOVE_ONLY_TYPE_FOR_CPP_03(File, RValue)

 public:
  // FLAG_(OPEN|CREATE).* are mutually exclusive. You should specify exactly one
  // of the five (possibly combining with other flags) when opening or creating
  // a file.
  // FLAG_(WRITE|APPEND) are mutually exclusive. This is so that APPEND behavior
  // will be consistent with O_APPEND on POSIX.
  // FLAG_EXCLUSIVE_(READ|WRITE) only grant exclusive access to the file on
  // creation on POSIX; for existing files, consider using Lock().
  enum Flags {
    FLAG_OPEN = 1 << 0,             // Opens a file, only if it exists.
    FLAG_CREATE = 1 << 1,           // Creates a new file, only if it does not
                                    // already exist.
    FLAG_OPEN_ALWAYS = 1 << 2,      // May create a new file.
    FLAG_CREATE_ALWAYS = 1 << 3,    // May overwrite an old file.
    FLAG_OPEN_TRUNCATED = 1 << 4,   // Opens a file and truncates it, only if it
                                    // exists.
    FLAG_READ = 1 << 5,
    FLAG_WRITE = 1 << 6,
    FLAG_APPEND = 1 << 7,
    FLAG_EXCLUSIVE_READ = 1 << 8,   // EXCLUSIVE is opposite of Windows SHARE.
    FLAG_EXCLUSIVE_WRITE = 1 << 9,
    FLAG_ASYNC = 1 << 10,
    FLAG_TEMPORARY = 1 << 11,       // Used on Windows only.
    FLAG_HIDDEN = 1 << 12,          // Used on Windows only.
    FLAG_DELETE_ON_CLOSE = 1 << 13,
    FLAG_WRITE_ATTRIBUTES = 1 << 14,  // Used on Windows only.
    FLAG_SHARE_DELETE = 1 << 15,      // Used on Windows only.
    FLAG_TERMINAL_DEVICE = 1 << 16,   // Serial port flags.
    FLAG_BACKUP_SEMANTICS = 1 << 17,  // Used on Windows only.
    FLAG_EXECUTE = 1 << 18,           // Used on Windows only.
  };

  // This enum has been recorded in multiple histograms. If the order of the
  // fields needs to change, please ensure that those histograms are obsolete or
  // have been moved to a different enum.
  //
  // FILE_ERROR_ACCESS_DENIED is returned when a call fails because of a
  // filesystem restriction. FILE_ERROR_SECURITY is returned when a browser
  // policy doesn't allow the operation to be executed.
  enum Error {
    FILE_OK = 0,
    FILE_ERROR_FAILED = -1,
    FILE_ERROR_IN_USE = -2,
    FILE_ERROR_EXISTS = -3,
    FILE_ERROR_NOT_FOUND = -4,
    FILE_ERROR_ACCESS_DENIED = -5,
    FILE_ERROR_TOO_MANY_OPENED = -6,
    FILE_ERROR_NO_MEMORY = -7,
    FILE_ERROR_NO_SPACE = -8,
    FILE_ERROR_NOT_A_DIRECTORY = -9,
    FILE_ERROR_INVALID_OPERATION = -10,
    FILE_ERROR_SECURITY = -11,
    FILE_ERROR_ABORT = -12,
    FILE_ERROR_NOT_A_FILE = -13,
    FILE_ERROR_NOT_EMPTY = -14,
    FILE_ERROR_INVALID_URL = -15,
    FILE_ERROR_IO = -16,
    // Put new entries here and increment FILE_ERROR_MAX.
    FILE_ERROR_MAX = -17
  };

  // This explicit mapping matches both FILE_ on Windows and SEEK_ on Linux.
  enum Whence {
    FROM_BEGIN   = 0,
    FROM_CURRENT = 1,
    FROM_END     = 2
  };

  // Used to hold information about a given file.
  // If you add more fields to this structure (platform-specific fields are OK),
  // make sure to update all functions that use it in file_util_{win|posix}.cc
  // too, and the ParamTraits<butil::PlatformFileInfo> implementation in
  // chrome/common/common_param_traits.cc.
  struct BUTIL_EXPORT Info {
    Info();
    ~Info();
#if defined(OS_POSIX)
    // Fills this struct with values from |stat_info|.
    void FromStat(const stat_wrapper_t& stat_info);
#endif

    // The size of the file in bytes.  Undefined when is_directory is true.
    int64_t size;

    // True if the file corresponds to a directory.
    bool is_directory;

    // True if the file corresponds to a symbolic link.
    bool is_symbolic_link;

    // The last modified time of a file.
    butil::Time last_modified;

    // The last accessed time of a file.
    butil::Time last_accessed;

    // The creation time of a file.
    butil::Time creation_time;
  };

  File();

  // Creates or opens the given file. This will fail with 'access denied' if the
  // |name| contains path traversal ('..') components.
  File(const FilePath& name, uint32_t flags);

  // Takes ownership of |platform_file|.
  explicit File(PlatformFile platform_file);

  // Creates an object with a specific error_details code.
  explicit File(Error error_details);

  // Move constructor for C++03 move emulation of this type.
  File(RValue other);

  ~File();

  // Move operator= for C++03 move emulation of this type.
  File& operator=(RValue other);

  // Creates or opens the given file.
  void Initialize(const FilePath& name, uint32_t flags);

  // Creates or opens the given file, allowing paths with traversal ('..')
  // components. Use only with extreme care.
  void InitializeUnsafe(const FilePath& name, uint32_t flags);

  bool IsValid() const;

  // Returns true if a new file was created (or an old one truncated to zero
  // length to simulate a new file, which can happen with
  // FLAG_CREATE_ALWAYS), and false otherwise.
  bool created() const { return created_; }

  // Returns the OS result of opening this file. Note that the way to verify
  // the success of the operation is to use IsValid(), not this method:
  //   File file(name, flags);
  //   if (!file.IsValid())
  //     return;
  Error error_details() const { return error_details_; }

  PlatformFile GetPlatformFile() const;
  PlatformFile TakePlatformFile();

  // Destroying this object closes the file automatically.
  void Close();

  // Changes current position in the file to an |offset| relative to an origin
  // defined by |whence|. Returns the resultant current position in the file
  // (relative to the start) or -1 in case of error.
  int64_t Seek(Whence whence, int64_t offset);

  // Reads the given number of bytes (or until EOF is reached) starting with the
  // given offset. Returns the number of bytes read, or -1 on error. Note that
  // this function makes a best effort to read all data on all platforms, so it
  // is not intended for stream oriented files but instead for cases when the
  // normal expectation is that actually |size| bytes are read unless there is
  // an error.
  int Read(int64_t offset, char* data, int size);

  // Same as above but without seek.
  int ReadAtCurrentPos(char* data, int size);

  // Reads the given number of bytes (or until EOF is reached) starting with the
  // given offset, but does not make any effort to read all data on all
  // platforms. Returns the number of bytes read, or -1 on error.
  int ReadNoBestEffort(int64_t offset, char* data, int size);

  // Same as above but without seek.
  int ReadAtCurrentPosNoBestEffort(char* data, int size);

  // Writes the given buffer into the file at the given offset, overwritting any
  // data that was previously there. Returns the number of bytes written, or -1
  // on error. Note that this function makes a best effort to write all data on
  // all platforms.
  // Ignores the offset and writes to the end of the file if the file was opened
  // with FLAG_APPEND.
  int Write(int64_t offset, const char* data, int size);

  // Save as above but without seek.
  int WriteAtCurrentPos(const char* data, int size);

  // Save as above but does not make any effort to write all data on all
  // platforms. Returns the number of bytes written, or -1 on error.
  int WriteAtCurrentPosNoBestEffort(const char* data, int size);

  // Returns the current size of this file, or a negative number on failure.
  int64_t GetLength();

  // Truncates the file to the given length. If |length| is greater than the
  // current size of the file, the file is extended with zeros. If the file
  // doesn't exist, |false| is returned.
  bool SetLength(int64_t length);

  // Flushes the buffers.
  bool Flush();

  // Updates the file times.
  bool SetTimes(Time last_access_time, Time last_modified_time);

  // Returns some basic information for the given file.
  bool GetInfo(Info* info);

  // Attempts to take an exclusive write lock on the file. Returns immediately
  // (i.e. does not wait for another process to unlock the file). If the lock
  // was obtained, the result will be FILE_OK. A lock only guarantees
  // that other processes may not also take a lock on the same file with the
  // same API - it may still be opened, renamed, unlinked, etc.
  //
  // Common semantics:
  //  * Locks are held by processes, but not inherited by child processes.
  //  * Locks are released by the OS on file close or process termination.
  //  * Locks are reliable only on local filesystems.
  //  * Duplicated file handles may also write to locked files.
  // Windows-specific semantics:
  //  * Locks are mandatory for read/write APIs, advisory for mapping APIs.
  //  * Within a process, locking the same file (by the same or new handle)
  //    will fail.
  // POSIX-specific semantics:
  //  * Locks are advisory only.
  //  * Within a process, locking the same file (by the same or new handle)
  //    will succeed.
  //  * Closing any descriptor on a given file releases the lock.
  Error Lock();

  // Unlock a file previously locked.
  Error Unlock();

  bool async() const { return async_; }

#if defined(OS_WIN)
  static Error OSErrorToFileError(DWORD last_error);
#elif defined(OS_POSIX)
  static Error OSErrorToFileError(int saved_errno);
#endif

  // Converts an error value to a human-readable form. Used for logging.
  static std::string ErrorToString(Error error);

 private:
  void SetPlatformFile(PlatformFile file);

#if defined(OS_WIN)
  win::ScopedHandle file_;
#elif defined(OS_POSIX)
  ScopedFD file_;
#endif

  Error error_details_;
  bool created_;
  bool async_;
};

}  // namespace butil

#endif  // BUTIL_FILES_FILE_H_
