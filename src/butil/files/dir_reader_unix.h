// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.


#ifndef BUTIL_FILES_DIR_READER_UNIX_H_
#define BUTIL_FILES_DIR_READER_UNIX_H_

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <dirent.h>

#include "butil/logging.h"
#include "butil/posix/eintr_wrapper.h"

// See the comments in dir_reader_posix.h about this.

namespace butil {

class DirReaderUnix {
 public:
  explicit DirReaderUnix(const char* directory_path)
      : fd_(open(directory_path, O_RDONLY | O_DIRECTORY)),
        dir_(NULL),current_(NULL) {
      dir_ = fdopendir(fd_);
  }

  ~DirReaderUnix() {
    if (fd_ >= 0) {
      if (IGNORE_EINTR(close(fd_)))
        RAW_LOG(ERROR, "Failed to close directory handle");
    }
    if(NULL != dir_){
        closedir(dir_);
    }
  }

  bool IsValid() const {
    return fd_ >= 0;
  }

  // Move to the next entry returning false if the iteration is complete.
  bool Next() {
    int err = readdir_r(dir_,&entry_, &current_);
    if(0 != err || NULL == current_){
        return false;
    }
    return true;
  }

  const char* name() const {
    if (NULL == current_)
      return NULL;
    return current_->d_name;
  }

  int fd() const {
    return fd_;
  }

  static bool IsFallback() {
    return false;
  }

 private:
  const int fd_;
  DIR* dir_;
  struct dirent entry_;
  struct dirent* current_;
};

}  // namespace butil

#endif // BUTIL_FILES_DIR_READER_LINUX_H_
