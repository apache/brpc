// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/file_util.h"

#include <errno.h>
#include <sys/vfs.h>

#include "butil/files/file_path.h"

#ifndef EXT2_SUPER_MAGIC
#define EXT2_SUPER_MAGIC 0xEF53
#endif
#ifndef MSDOS_SUPER_MAGIC
#define MSDOS_SUPER_MAGIC 0x4d44		/* MD */
#endif
#ifndef REISERFS_SUPER_MAGIC
#define REISERFS_SUPER_MAGIC 0x52654973	/* used by gcc */
#endif
#ifndef NFS_SUPER_MAGIC
#define NFS_SUPER_MAGIC 0x6969
#endif
#ifndef SMB_SUPER_MAGIC
#define SMB_SUPER_MAGIC 0x517B
#endif
#ifndef CODA_SUPER_MAGIC
#define CODA_SUPER_MAGIC 0x73757245
#endif
#ifndef CGROUP_SUPER_MAGIC
#define CGROUP_SUPER_MAGIC 0x27e0eb
#endif
#ifndef BTRFS_SUPER_MAGIC
#define BTRFS_SUPER_MAGIC 0x9123683E
#endif
#ifndef HUGETLBFS_MAGIC
#define HUGETLBFS_MAGIC 0x958458f6
#endif
#ifndef RAMFS_MAGIC
#define RAMFS_MAGIC 0x858458f6
#endif
#ifndef TMPFS_MAGIC
#define TMPFS_MAGIC 0x01021994
#endif

namespace butil {

bool GetFileSystemType(const FilePath& path, FileSystemType* type) {
  struct statfs statfs_buf;
  if (statfs(path.value().c_str(), &statfs_buf) < 0) {
    if (errno == ENOENT)
      return false;
    *type = FILE_SYSTEM_UNKNOWN;
    return true;
  }

  // Not all possible |statfs_buf.f_type| values are in linux/magic.h.
  // Missing values are copied from the statfs man page.
  switch (statfs_buf.f_type) {
    case 0:
      *type = FILE_SYSTEM_0;
      break;
    case EXT2_SUPER_MAGIC:  // Also ext3 and ext4
    case MSDOS_SUPER_MAGIC:
    case REISERFS_SUPER_MAGIC:
    case BTRFS_SUPER_MAGIC:
    case 0x5346544E:  // NTFS
    case 0x58465342:  // XFS
    case 0x3153464A:  // JFS
      *type = FILE_SYSTEM_ORDINARY;
      break;
    case NFS_SUPER_MAGIC:
      *type = FILE_SYSTEM_NFS;
      break;
    case SMB_SUPER_MAGIC:
    case 0xFF534D42:  // CIFS
      *type = FILE_SYSTEM_SMB;
      break;
    case CODA_SUPER_MAGIC:
      *type = FILE_SYSTEM_CODA;
      break;
    case HUGETLBFS_MAGIC:
    case RAMFS_MAGIC:
    case TMPFS_MAGIC:
      *type = FILE_SYSTEM_MEMORY;
      break;
    case CGROUP_SUPER_MAGIC:
      *type = FILE_SYSTEM_CGROUP;
      break;
    default:
      *type = FILE_SYSTEM_OTHER;
  }
  return true;
}

}  // namespace butil
