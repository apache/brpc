// Copyright (c) 2010 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Author: Ge,Jun (gejun@baidu.com)
// Date: 2010/05/29

// Watch timestamp of a file

#ifndef BUTIL_FILES_FILE_WATCHER_H
#define BUTIL_FILES_FILE_WATCHER_H

#include <stdint.h>                                 // int64_t
#include <string>                                   // std::string

// Example:
//   FileWatcher fw;
//   fw.init("to_be_watched_file");
//   ....
//   if (fw.check_and_consume() > 0) {
//       // the file is created or updated 
//       ......
//   }

namespace butil {
class FileWatcher {
public:
    enum Change {
        DELETED = -1,
        UNCHANGED = 0,
        UPDATED = 1,
        CREATED = 2,
    };

    typedef int64_t Timestamp;
    
    FileWatcher();

    // Watch file at `file_path', must be called before calling other methods.
    // Returns 0 on success, -1 otherwise.
    int init(const char* file_path);
    // Let check_and_consume returns CREATE when file_path already exists.
    int init_from_not_exist(const char* file_path);

    // Check and consume change of the watched file. Write `last_timestamp'
    // if it's not NULL.
    // Returns:
    //   CREATE    the file is created since last call to this method.
    //   UPDATED   the file is modified since last call.
    //   UNCHANGED the file has no change since last call.
    //   DELETED   the file was deleted since last call.
    // Note: If the file is updated too frequently, this method may return 
    // UNCHANGED due to precision of stat(2) and the file system. If the file
    // is created and deleted too frequently, the event may not be detected.
    Change check_and_consume(Timestamp* last_timestamp = NULL);

    // Set internal timestamp. User can use this method to make
    // check_and_consume() replay the change.
    void restore(Timestamp timestamp);
    
    // Get path of watched file
    const char* filepath() const { return _file_path.c_str(); }

private:
    Change check(Timestamp* new_timestamp) const;

    std::string _file_path;
    Timestamp _last_ts;    
};
}  // namespace butil

#endif  // BUTIL_FILES_FILE_WATCHER_H
