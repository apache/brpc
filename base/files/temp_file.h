// Copyright (c) 2010 Baidu.com, Inc. All Rights Reserved
//
// Create a temporary file in current directory, which will be deleted when 
// corresponding TempFile object destructs, typically for unit testing.
// 
// Usage:
//   { 
//      TempFile tmpfile;           // A temporay file shall be created
//      tmpfile.save("some text");  // Write into the temporary file
//   }
//   // The temporary file shall be removed due to destruction of tmpfile
// 
// Author: Yan,Lin (yanlin@baidu.com),  Ge,Jun (gejun@baidu.com)
// Date: Thu Oct 28 15:23:09 2010

#ifndef BASE_FILES_TEMP_FILE_H
#define BASE_FILES_TEMP_FILE_H

#include "base/macros.h"

namespace base {
    
class TempFile {
public:
    // Create a temporary file in current directory. If |ext| is given,
    // filename will be temp_file_XXXXXX.|ext|, temp_file_XXXXXX otherwise.
    // If temporary file cannot be created, all save*() functions will
    // return -1. If |ext| is too long, filename will be truncated.
    TempFile();
    explicit TempFile(const char* ext);

    // The temporary file is removed in destructor.
    ~TempFile();

    // Save |content| to file, overwriting existing file.
    // Returns 0 when successful, -1 otherwise.
    int save(const char *content);

    // Save |fmt| and associated values to file, overwriting existing file.
    // Returns 0 when successful, -1 otherwise.
    int save_format(const char *fmt, ...) __attribute__((format (printf, 2, 3) ));

    // Save binary data |buf| (|count| bytes) to file, overwriting existing file.
    // Returns 0 when successful, -1 otherwise.
    int save_bin(const void *buf, size_t count);
    
    // Get name of the temporary file.
    const char *fname() const { return _fname; }

private:
    // TempFile is associated with file, copying makes no sense.
    DISALLOW_COPY_AND_ASSIGN(TempFile);
    
    int _reopen_if_necessary();
    
    int _fd;                // file descriptor
    int _ever_opened;
    char _fname[24];        // name of the file
};

} // namespace base

#endif  // BASE_FILES_TEMP_FILE_H
