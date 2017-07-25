// Copyright (c) 2010 Baidu.com, Inc. All Rights Reserved
// Watch timestamp of a file

// Author: Ge,Jun (gejun@baidu.com)
// Date: 2010/05/29

#ifndef BASE_FILES_FILE_WATCHER_H
#define BASE_FILES_FILE_WATCHER_H

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

namespace base {
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
}  // namespace base

#endif  // BASE_FILES_FILE_WATCHER_H
