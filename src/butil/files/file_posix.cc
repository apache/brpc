// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/files/file.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "butil/files/file_path.h"
#include "butil/logging.h"
#include "butil/posix/eintr_wrapper.h"
#include "butil/strings/utf_string_conversions.h"
#include "butil/threading/thread_restrictions.h"

#if defined(OS_ANDROID)
#include "butil/os_compat_android.h"
#endif

namespace butil {

// Make sure our Whence mappings match the system headers.
COMPILE_ASSERT(File::FROM_BEGIN   == SEEK_SET &&
               File::FROM_CURRENT == SEEK_CUR &&
               File::FROM_END     == SEEK_END, whence_matches_system);

namespace {

#if defined(OS_BSD) || defined(OS_MACOSX) || defined(OS_NACL)
static int CallFstat(int fd, stat_wrapper_t *sb) {
  butil::ThreadRestrictions::AssertIOAllowed();
  return fstat(fd, sb);
}
#else
static int CallFstat(int fd, stat_wrapper_t *sb) {
  butil::ThreadRestrictions::AssertIOAllowed();
  return fstat64(fd, sb);
}
#endif

// NaCl doesn't provide the following system calls, so either simulate them or
// wrap them in order to minimize the number of #ifdef's in this file.
#if !defined(OS_NACL)
static bool IsOpenAppend(PlatformFile file) {
  return (fcntl(file, F_GETFL) & O_APPEND) != 0;
}

static int CallFtruncate(PlatformFile file, int64_t length) {
  return HANDLE_EINTR(ftruncate(file, length));
}

static int CallFsync(PlatformFile file) {
  return HANDLE_EINTR(fsync(file));
}

static int CallFutimes(PlatformFile file, const struct timeval times[2]) {
#ifdef __USE_XOPEN2K8
  // futimens should be available, but futimes might not be
  // http://pubs.opengroup.org/onlinepubs/9699919799/

  timespec ts_times[2];
  ts_times[0].tv_sec  = times[0].tv_sec;
  ts_times[0].tv_nsec = times[0].tv_usec * 1000;
  ts_times[1].tv_sec  = times[1].tv_sec;
  ts_times[1].tv_nsec = times[1].tv_usec * 1000;

  return futimens(file, ts_times);
#else
  return futimes(file, times);
#endif
}

static File::Error CallFctnlFlock(PlatformFile file, bool do_lock) {
  struct flock lock;
  lock.l_type = F_WRLCK;
  lock.l_whence = SEEK_SET;
  lock.l_start = 0;
  lock.l_len = 0;  // Lock entire file.
  if (HANDLE_EINTR(fcntl(file, do_lock ? F_SETLK : F_UNLCK, &lock)) == -1)
    return File::OSErrorToFileError(errno);
  return File::FILE_OK;
}
#else  // defined(OS_NACL)

static bool IsOpenAppend(PlatformFile file) {
  // NaCl doesn't implement fcntl. Since NaCl's write conforms to the POSIX
  // standard and always appends if the file is opened with O_APPEND, just
  // return false here.
  return false;
}

static int CallFtruncate(PlatformFile file, int64_t length) {
  NOTIMPLEMENTED();  // NaCl doesn't implement ftruncate.
  return 0;
}

static int CallFsync(PlatformFile file) {
  NOTIMPLEMENTED();  // NaCl doesn't implement fsync.
  return 0;
}

static int CallFutimes(PlatformFile file, const struct timeval times[2]) {
  NOTIMPLEMENTED();  // NaCl doesn't implement futimes.
  return 0;
}

static File::Error CallFctnlFlock(PlatformFile file, bool do_lock) {
  NOTIMPLEMENTED();  // NaCl doesn't implement flock struct.
  return File::FILE_ERROR_INVALID_OPERATION;
}
#endif  // defined(OS_NACL)

}  // namespace

void File::Info::FromStat(const stat_wrapper_t& stat_info) {
  is_directory = S_ISDIR(stat_info.st_mode);
  is_symbolic_link = S_ISLNK(stat_info.st_mode);
  size = stat_info.st_size;

#if defined(OS_LINUX)
  time_t last_modified_sec = stat_info.st_mtim.tv_sec;
  int64_t last_modified_nsec = stat_info.st_mtim.tv_nsec;
  time_t last_accessed_sec = stat_info.st_atim.tv_sec;
  int64_t last_accessed_nsec = stat_info.st_atim.tv_nsec;
  time_t creation_time_sec = stat_info.st_ctim.tv_sec;
  int64_t creation_time_nsec = stat_info.st_ctim.tv_nsec;
#elif defined(OS_ANDROID)
  time_t last_modified_sec = stat_info.st_mtime;
  int64_t last_modified_nsec = stat_info.st_mtime_nsec;
  time_t last_accessed_sec = stat_info.st_atime;
  int64_t last_accessed_nsec = stat_info.st_atime_nsec;
  time_t creation_time_sec = stat_info.st_ctime;
  int64_t creation_time_nsec = stat_info.st_ctime_nsec;
#elif defined(OS_MACOSX) || defined(OS_IOS) || defined(OS_BSD)
  time_t last_modified_sec = stat_info.st_mtimespec.tv_sec;
  int64_t last_modified_nsec = stat_info.st_mtimespec.tv_nsec;
  time_t last_accessed_sec = stat_info.st_atimespec.tv_sec;
  int64_t last_accessed_nsec = stat_info.st_atimespec.tv_nsec;
  time_t creation_time_sec = stat_info.st_ctimespec.tv_sec;
  int64_t creation_time_nsec = stat_info.st_ctimespec.tv_nsec;
#else
  time_t last_modified_sec = stat_info.st_mtime;
  int64_t last_modified_nsec = 0;
  time_t last_accessed_sec = stat_info.st_atime;
  int64_t last_accessed_nsec = 0;
  time_t creation_time_sec = stat_info.st_ctime;
  int64_t creation_time_nsec = 0;
#endif

  last_modified =
      Time::FromTimeT(last_modified_sec) +
      TimeDelta::FromMicroseconds(last_modified_nsec /
                                  Time::kNanosecondsPerMicrosecond);

  last_accessed =
      Time::FromTimeT(last_accessed_sec) +
      TimeDelta::FromMicroseconds(last_accessed_nsec /
                                  Time::kNanosecondsPerMicrosecond);

  creation_time =
      Time::FromTimeT(creation_time_sec) +
      TimeDelta::FromMicroseconds(creation_time_nsec /
                                  Time::kNanosecondsPerMicrosecond);
}

// NaCl doesn't implement system calls to open files directly.
#if !defined(OS_NACL)
// TODO(erikkay): does it make sense to support FLAG_EXCLUSIVE_* here?
void File::InitializeUnsafe(const FilePath& name, uint32_t flags) {
  butil::ThreadRestrictions::AssertIOAllowed();
  DCHECK(!IsValid());

  int open_flags = 0;
  if (flags & FLAG_CREATE)
    open_flags = O_CREAT | O_EXCL;

  created_ = false;

  if (flags & FLAG_CREATE_ALWAYS) {
    DCHECK(!open_flags);
    DCHECK(flags & FLAG_WRITE);
    open_flags = O_CREAT | O_TRUNC;
  }

  if (flags & FLAG_OPEN_TRUNCATED) {
    DCHECK(!open_flags);
    DCHECK(flags & FLAG_WRITE);
    open_flags = O_TRUNC;
  }

  if (!open_flags && !(flags & FLAG_OPEN) && !(flags & FLAG_OPEN_ALWAYS)) {
    NOTREACHED();
    errno = EOPNOTSUPP;
    error_details_ = FILE_ERROR_FAILED;
    return;
  }

  if (flags & FLAG_WRITE && flags & FLAG_READ) {
    open_flags |= O_RDWR;
  } else if (flags & FLAG_WRITE) {
    open_flags |= O_WRONLY;
  } else if (!(flags & FLAG_READ) &&
             !(flags & FLAG_WRITE_ATTRIBUTES) &&
             !(flags & FLAG_APPEND) &&
             !(flags & FLAG_OPEN_ALWAYS)) {
    NOTREACHED();
  }

  if (flags & FLAG_TERMINAL_DEVICE)
    open_flags |= O_NOCTTY | O_NDELAY;

  if (flags & FLAG_APPEND && flags & FLAG_READ)
    open_flags |= O_APPEND | O_RDWR;
  else if (flags & FLAG_APPEND)
    open_flags |= O_APPEND | O_WRONLY;

  COMPILE_ASSERT(O_RDONLY == 0, O_RDONLY_must_equal_zero);

  int mode = S_IRUSR | S_IWUSR;
#if defined(OS_CHROMEOS)
  mode |= S_IRGRP | S_IROTH;
#endif

  int descriptor = HANDLE_EINTR(open(name.value().c_str(), open_flags, mode));

  if (flags & FLAG_OPEN_ALWAYS) {
    if (descriptor < 0) {
      open_flags |= O_CREAT;
      if (flags & FLAG_EXCLUSIVE_READ || flags & FLAG_EXCLUSIVE_WRITE)
        open_flags |= O_EXCL;   // together with O_CREAT implies O_NOFOLLOW

      descriptor = HANDLE_EINTR(open(name.value().c_str(), open_flags, mode));
      if (descriptor >= 0)
        created_ = true;
    }
  }

  if (descriptor < 0) {
    error_details_ = File::OSErrorToFileError(errno);
    return;
  }

  if (flags & (FLAG_CREATE_ALWAYS | FLAG_CREATE))
    created_ = true;

  if (flags & FLAG_DELETE_ON_CLOSE)
    unlink(name.value().c_str());

  async_ = ((flags & FLAG_ASYNC) == FLAG_ASYNC);
  error_details_ = FILE_OK;
  file_.reset(descriptor);
}
#endif  // !defined(OS_NACL)

bool File::IsValid() const {
  return file_.is_valid();
}

PlatformFile File::GetPlatformFile() const {
  return file_.get();
}

PlatformFile File::TakePlatformFile() {
  return file_.release();
}

void File::Close() {
  if (!IsValid())
    return;

  butil::ThreadRestrictions::AssertIOAllowed();
  file_.reset();
}

int64_t File::Seek(Whence whence, int64_t offset) {
  butil::ThreadRestrictions::AssertIOAllowed();
  DCHECK(IsValid());

#if defined(OS_ANDROID)
  COMPILE_ASSERT(sizeof(int64_t) == sizeof(off64_t), off64_t_64_bit);
  return lseek64(file_.get(), static_cast<off64_t>(offset),
                 static_cast<int>(whence));
#else
  COMPILE_ASSERT(sizeof(int64_t) == sizeof(off_t), off_t_64_bit);
  return lseek(file_.get(), static_cast<off_t>(offset),
               static_cast<int>(whence));
#endif
}

int File::Read(int64_t offset, char* data, int size) {
  butil::ThreadRestrictions::AssertIOAllowed();
  DCHECK(IsValid());
  if (size < 0)
    return -1;

  int bytes_read = 0;
  int rv;
  do {
    rv = HANDLE_EINTR(pread(file_.get(), data + bytes_read,
                            size - bytes_read, offset + bytes_read));
    if (rv <= 0)
      break;

    bytes_read += rv;
  } while (bytes_read < size);

  return bytes_read ? bytes_read : rv;
}

int File::ReadAtCurrentPos(char* data, int size) {
  butil::ThreadRestrictions::AssertIOAllowed();
  DCHECK(IsValid());
  if (size < 0)
    return -1;

  int bytes_read = 0;
  int rv;
  do {
    rv = HANDLE_EINTR(read(file_.get(), data + bytes_read, size - bytes_read));
    if (rv <= 0)
      break;

    bytes_read += rv;
  } while (bytes_read < size);

  return bytes_read ? bytes_read : rv;
}

int File::ReadNoBestEffort(int64_t offset, char* data, int size) {
  butil::ThreadRestrictions::AssertIOAllowed();
  DCHECK(IsValid());

  return HANDLE_EINTR(pread(file_.get(), data, size, offset));
}

int File::ReadAtCurrentPosNoBestEffort(char* data, int size) {
  butil::ThreadRestrictions::AssertIOAllowed();
  DCHECK(IsValid());
  if (size < 0)
    return -1;

  return HANDLE_EINTR(read(file_.get(), data, size));
}

int File::Write(int64_t offset, const char* data, int size) {
  butil::ThreadRestrictions::AssertIOAllowed();

  if (IsOpenAppend(file_.get()))
    return WriteAtCurrentPos(data, size);

  DCHECK(IsValid());
  if (size < 0)
    return -1;

  int bytes_written = 0;
  int rv;
  do {
    rv = HANDLE_EINTR(pwrite(file_.get(), data + bytes_written,
                             size - bytes_written, offset + bytes_written));
    if (rv <= 0)
      break;

    bytes_written += rv;
  } while (bytes_written < size);

  return bytes_written ? bytes_written : rv;
}

int File::WriteAtCurrentPos(const char* data, int size) {
  butil::ThreadRestrictions::AssertIOAllowed();
  DCHECK(IsValid());
  if (size < 0)
    return -1;

  int bytes_written = 0;
  int rv;
  do {
    rv = HANDLE_EINTR(write(file_.get(), data + bytes_written,
                            size - bytes_written));
    if (rv <= 0)
      break;

    bytes_written += rv;
  } while (bytes_written < size);

  return bytes_written ? bytes_written : rv;
}

int File::WriteAtCurrentPosNoBestEffort(const char* data, int size) {
  butil::ThreadRestrictions::AssertIOAllowed();
  DCHECK(IsValid());
  if (size < 0)
    return -1;

  return HANDLE_EINTR(write(file_.get(), data, size));
}

int64_t File::GetLength() {
  DCHECK(IsValid());

  stat_wrapper_t file_info;
  if (CallFstat(file_.get(), &file_info))
    return false;

  return file_info.st_size;
}

bool File::SetLength(int64_t length) {
  butil::ThreadRestrictions::AssertIOAllowed();
  DCHECK(IsValid());
  return !CallFtruncate(file_.get(), length);
}

bool File::Flush() {
  butil::ThreadRestrictions::AssertIOAllowed();
  DCHECK(IsValid());
  return !CallFsync(file_.get());
}

bool File::SetTimes(Time last_access_time, Time last_modified_time) {
  butil::ThreadRestrictions::AssertIOAllowed();
  DCHECK(IsValid());

  timeval times[2];
  times[0] = last_access_time.ToTimeVal();
  times[1] = last_modified_time.ToTimeVal();

  return !CallFutimes(file_.get(), times);
}

bool File::GetInfo(Info* info) {
  DCHECK(IsValid());

  stat_wrapper_t file_info;
  if (CallFstat(file_.get(), &file_info))
    return false;

  info->FromStat(file_info);
  return true;
}

File::Error File::Lock() {
  return CallFctnlFlock(file_.get(), true);
}

File::Error File::Unlock() {
  return CallFctnlFlock(file_.get(), false);
}

// Static.
File::Error File::OSErrorToFileError(int saved_errno) {
  switch (saved_errno) {
    case EACCES:
    case EISDIR:
    case EROFS:
    case EPERM:
      return FILE_ERROR_ACCESS_DENIED;
#if !defined(OS_NACL)  // ETXTBSY not defined by NaCl.
    case ETXTBSY:
      return FILE_ERROR_IN_USE;
#endif
    case EEXIST:
      return FILE_ERROR_EXISTS;
    case ENOENT:
      return FILE_ERROR_NOT_FOUND;
    case EMFILE:
      return FILE_ERROR_TOO_MANY_OPENED;
    case ENOMEM:
      return FILE_ERROR_NO_MEMORY;
    case ENOSPC:
      return FILE_ERROR_NO_SPACE;
    case ENOTDIR:
      return FILE_ERROR_NOT_A_DIRECTORY;
    default:
      return FILE_ERROR_FAILED;
  }
}

void File::SetPlatformFile(PlatformFile file) {
  DCHECK(!file_.is_valid());
  file_.reset(file);
}

}  // namespace butil
